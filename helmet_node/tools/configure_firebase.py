"""Configure local ESP-IDF settings; optionally verify a disposable RTDB node.

Never prints credentials or request URLs. Does not edit security rules, user
records, live telemetry, control commands or emergency history.
"""
import argparse
import json
import re
import ssl
import sys
import uuid
from pathlib import Path
from urllib.parse import quote, urlsplit
from urllib.request import Request, build_opener, HTTPSHandler, HTTPRedirectHandler
from urllib.error import HTTPError, URLError


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--key-file', required=True, type=Path)
    ap.add_argument('--database-url', required=True)
    ap.add_argument('--device-id', default='Helmet_001')
    ap.add_argument('--sdkconfig', type=Path)
    ap.add_argument('--smoke', action='store_true')
    args = ap.parse_args()
    base = args.database_url.rstrip('/')
    url = urlsplit(base)
    if (url.scheme != 'https' or not url.hostname or url.username or url.password
            or url.port or url.path or url.query or url.fragment
            or not re.fullmatch(r'[a-z0-9-]+(?:\.[a-z0-9-]+)?\.(?:firebaseio\.com|firebasedatabase\.app)', url.hostname)):
        raise ValueError('Expected Firebase HTTPS database root URL')
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,64}', args.device_id):
        raise ValueError('Invalid device ID')
    text = args.key_file.read_text(encoding='utf-8-sig')
    match = re.search(r'^.*ADMIN\s*\(AUTH\)\s*:\s*(\S+)\s*$', text, re.M | re.I)
    if not match:
        raise ValueError('ADMIN (AUTH) entry missing from key file')
    secret = match.group(1)
    if not re.fullmatch(r'[A-Za-z0-9_-]{40}', secret):
        raise ValueError('Expected a 40-character legacy database secret, not an API key/ID token')
    if args.sdkconfig:
        original = args.sdkconfig.read_text(encoding='utf-8')
        settings = {
            'CONFIG_SMART_HELMET_WIFI_ENABLE': 'y',
            'CONFIG_SMART_HELMET_CLOUD_ENABLE': 'y',
            'CONFIG_SMART_HELMET_CLOUD_FIREBASE': 'y',
            'CONFIG_SMART_HELMET_DEVICE_ID': json.dumps(args.device_id),
            'CONFIG_SMART_HELMET_CLOUD_URL': json.dumps(base),
            'CONFIG_SMART_HELMET_CLOUD_AUTH_TOKEN': json.dumps(secret),
            'CONFIG_SMART_HELMET_CLOUD_TELEMETRY_INTERVAL_MS': '5000',
        }
        for key, value in settings.items():
            original = re.sub(rf'^(?:{key}=.*|# {key} is not set)\n?', '', original, flags=re.M)
            original += f'{key}={value}\n'
        args.sdkconfig.write_text(original, encoding='utf-8')
        ssid = re.search(r'^CONFIG_SMART_HELMET_WIFI_SSID="(.+)"$', original, re.M)
        print('CONFIGURED: Firebase root/device/auth; credentials not printed')
        print('Wi-Fi SSID: ' + ('present (not connection proof)' if ssid else 'MISSING; set through menuconfig'))
    if not args.smoke:
        return
    opener = build_opener(HTTPSHandler(context=ssl.create_default_context()), NoRedirect())
    def request(method, path, data=None):
        address = base + '/' + path + '.json?auth=' + quote(secret, safe='')
        body = None if data is None else json.dumps(data).encode('utf-8')
        req = Request(address, data=body, method=method, headers={'Content-Type': 'application/json'})
        with opener.open(req, timeout=20) as response:
            return json.loads(response.read() or b'null')
    # Verify the existing device, without exposing owner UID.
    info = request('GET', f'Devices/{args.device_id}/info')
    if not isinstance(info, dict) or 'owner_uid' not in info:
        raise ValueError('Expected device info not found; refusing write probe')
    print('LIVE READ PASS: authenticated existing device info')
    path = f'Diagnostics/{args.device_id}/codex_{uuid.uuid4().hex}'
    payload = {'test': 'firebase_rest_connectivity', 'hardware_verified': False,
               'timestamp': {'.sv': 'timestamp'}}
    try:
        request('PATCH', path, payload)
        result = request('GET', path)
        if not isinstance(result, dict) or result.get('test') != payload['test'] or not isinstance(result.get('timestamp'), int):
            raise ValueError('Diagnostic readback mismatch')
        print('LIVE WRITE/READ PASS: PATCH, auth, server timestamp; PC network only')
    finally:
        request('DELETE', path)
        if request('GET', path) is not None:
            raise ValueError('Diagnostic cleanup not confirmed')
        print('CLEANUP PASS: disposable diagnostic removed')


if __name__ == '__main__':
    try:
        main()
    except HTTPError as exc:
        print(f'FAILED: Firebase HTTP {exc.code}; URL/credentials suppressed', file=sys.stderr)
        sys.exit(1)
    except (URLError, OSError):
        print('FAILED: network, TLS or local file access; details suppressed to protect credentials', file=sys.stderr)
        sys.exit(1)
    except ValueError as exc:
        print(f'FAILED: {exc}', file=sys.stderr)
        sys.exit(1)

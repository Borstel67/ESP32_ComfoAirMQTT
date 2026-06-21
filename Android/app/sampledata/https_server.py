#!/usr/bin/env python3
"""
HTTPS Server für PWA Development
Starte mit: python https_server.py
Dann öffne: https://localhost:8443
"""

import http.server
import ssl
import os
from pathlib import Path

# Pfad zum sampledata Verzeichnis
BASE_DIR = Path(__file__).parent

PORT = 8443

# Self-Signed Zertifikat generieren falls nicht vorhanden
CERT_FILE = BASE_DIR / "localhost.crt"
KEY_FILE = BASE_DIR / "localhost.key"

def generate_self_signed_cert():
    """Generiere Self-Signed SSL-Zertifikat mit Python cryptography"""
    if CERT_FILE.exists() and KEY_FILE.exists():
        print(f"✓ SSL-Zertifikat existiert bereits")
        return
    
    print("📝 Generiere Self-Signed SSL-Zertifikat...")
    
    try:
        from cryptography import x509
        from cryptography.x509.oid import NameOID, ExtensionOID
        from cryptography.hazmat.primitives import hashes
        from cryptography.hazmat.backends import default_backend
        from cryptography.hazmat.primitives.asymmetric import rsa
        from cryptography.hazmat.primitives import serialization
        import datetime
    except ImportError:
        print("❌ cryptography Modul nicht gefunden")
        print("Installiere: pip install cryptography")
        import subprocess
        subprocess.run(["python", "-m", "pip", "install", "cryptography", "-q"])
        print("✓ cryptography installiert, starte neu...")
        import sys
        sys.exit(0)
    
    # Private Key generieren
    private_key = rsa.generate_private_key(
        public_exponent=65537,
        key_size=2048,
        backend=default_backend()
    )
    
    # Zertifikat erstellen
    subject = issuer = x509.Name([
        x509.NameAttribute(NameOID.COUNTRY_NAME, "DE"),
        x509.NameAttribute(NameOID.STATE_OR_PROVINCE_NAME, "Local"),
        x509.NameAttribute(NameOID.LOCALITY_NAME, "Local"),
        x509.NameAttribute(NameOID.ORGANIZATION_NAME, "ComfoAir PWA"),
        x509.NameAttribute(NameOID.COMMON_NAME, "localhost"),
    ])
    
    cert = x509.CertificateBuilder().subject_name(
        subject
    ).issuer_name(
        issuer
    ).public_key(
        private_key.public_key()
    ).serial_number(
        x509.random_serial_number()
    ).not_valid_before(
        datetime.datetime.utcnow()
    ).not_valid_after(
        datetime.datetime.utcnow() + datetime.timedelta(days=365)
    ).add_extension(
        x509.SubjectAlternativeName([
            x509.DNSName("localhost"),
            x509.DNSName("127.0.0.1"),
        ]),
        critical=False,
    ).sign(private_key, hashes.SHA256(), default_backend())
    
    # Speichern
    with open(KEY_FILE, "wb") as f:
        f.write(private_key.private_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PrivateFormat.TraditionalOpenSSL,
            encryption_algorithm=serialization.NoEncryption()
        ))
    
    with open(CERT_FILE, "wb") as f:
        f.write(cert.public_bytes(serialization.Encoding.PEM))
    
    print(f"✓ Zertifikat erstellt: {CERT_FILE}")
    print(f"✓ Private Key: {KEY_FILE}")

class PWAHTTPRequestHandler(http.server.SimpleHTTPRequestHandler):
    """HTTP Handler mit Directory als root"""
    
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(BASE_DIR), **kwargs)
    
    def end_headers(self):
        # Cache-Headers für PWA
        self.send_header('Cache-Control', 'no-cache, no-store, must-revalidate')
        self.send_header('Service-Worker-Allowed', '/')
        super().end_headers()
    
    def log_message(self, format, *args):
        # Schönere Logs
        print(f"[{self.client_address[0]:s}] {format % args}")

def main():
    generate_self_signed_cert()
    
    # SSL-Kontext konfigurieren
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(str(CERT_FILE), str(KEY_FILE))
    
    # Server starten
    with http.server.HTTPServer(("0.0.0.0", PORT), PWAHTTPRequestHandler) as httpd:
        print(f"\n🚀 PWA HTTPS Server läuft!")
        print(f"📍 URL: https://localhost:{PORT}")
        print(f"📍 IP:  https://{{LOCAL_IP}}:{PORT}")
        print(f"\n⚠️  Browser-Warnung ignorieren (Self-Signed Zertifikat)")
        print(f"✓ Service Worker & PWA Features aktiviert")
        print(f"\n💡 Tipps:")
        print(f"   - Android-Device: https://{{ROUTER_IP}}:{PORT}")
        print(f"   - Dev Tools öffnen: F12")
        print(f"   - Offline testen: Network → Offline aktivieren")
        print(f"\nDrücke CTRL+C zum Beenden...\n")
        
        httpd.socket = context.wrap_socket(httpd.socket, server_side=True)
        
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\n✓ Server beendet")

if __name__ == "__main__":
    main()

package com.mundox.wireless;

import android.content.Context;
import io.github.muntashirakon.adb.AbsAdbConnectionManager;
import org.bouncycastle.asn1.x500.X500Name;
import org.bouncycastle.cert.jcajce.JcaX509CertificateConverter;
import org.bouncycastle.cert.jcajce.JcaX509v3CertificateBuilder;
import org.bouncycastle.operator.ContentSigner;
import org.bouncycastle.operator.jcajce.JcaContentSignerBuilder;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.math.BigInteger;
import java.security.KeyFactory;
import java.security.KeyPair;
import java.security.KeyPairGenerator;
import java.security.PrivateKey;
import java.security.SecureRandom;
import java.security.cert.Certificate;
import java.security.cert.CertificateFactory;
import java.security.spec.PKCS8EncodedKeySpec;
import java.util.Date;
import java.util.concurrent.TimeUnit;

/**
 * A persistent ADB host identity, stored only in this app's private storage.
 * Regenerating the key pair would require re-pairing with the Watch4.
 */
final class AdbIdentity extends AbsAdbConnectionManager {
    private final PrivateKey key;
    private final Certificate cert;

    AdbIdentity(Context context) throws Exception {
        File identityDir = new File(context.getFilesDir(), "adb-identity");
        if (!identityDir.isDirectory() && !identityDir.mkdirs()) {
            throw new IllegalStateException("No se pudo crear almacenamiento privado para ADB");
        }
        File keyFile = new File(identityDir, "adb-private.pk8");
        File certFile = new File(identityDir, "adb-certificate.der");
        if (keyFile.exists() && certFile.exists()) {
            byte[] keyBytes = readSmall(keyFile, 16384);
            byte[] certBytes = readSmall(certFile, 16384);
            key = KeyFactory.getInstance("RSA")
                    .generatePrivate(new PKCS8EncodedKeySpec(keyBytes));
            cert = CertificateFactory.getInstance("X.509")
                    .generateCertificate(new java.io.ByteArrayInputStream(certBytes));
        } else {
            KeyPairGenerator generator = KeyPairGenerator.getInstance("RSA");
            generator.initialize(2048, new SecureRandom());
            KeyPair identity = generator.generateKeyPair();
            Date from = new Date(System.currentTimeMillis() - 60_000L);
            Date to = new Date(System.currentTimeMillis() + 10L * 365 * 24 * 60 * 60 * 1000);
            X500Name name = new X500Name("CN=Mundo X Wireless ADB");
            JcaX509v3CertificateBuilder builder = new JcaX509v3CertificateBuilder(
                    name, new BigInteger(96, new SecureRandom()).abs().add(BigInteger.ONE),
                    from, to, name, identity.getPublic());
            ContentSigner signer = new JcaContentSignerBuilder("SHA256withRSA")
                    .build(identity.getPrivate());
            Certificate generated = new JcaX509CertificateConverter()
                    .getCertificate(builder.build(signer));
            writePrivate(keyFile, identity.getPrivate().getEncoded());
            writePrivate(certFile, generated.getEncoded());
            key = identity.getPrivate();
            cert = generated;
        }
        // TLSv1.3 mode for modern Wear OS ADB daemons.
        setApi(33);
        setTimeout(18, TimeUnit.SECONDS);
        setThrowOnUnauthorised(true);
    }

    private static byte[] readSmall(File file, int limit) throws Exception {
        if (file.length() < 1 || file.length() > limit) {
            throw new IllegalStateException("Credenciales ADB incompletas");
        }
        try (FileInputStream input = new FileInputStream(file);
             java.io.ByteArrayOutputStream output = new java.io.ByteArrayOutputStream()) {
            byte[] buffer = new byte[2048];
            int n;
            while ((n = input.read(buffer)) != -1) {
                if (output.size() + n > limit) throw new IllegalStateException("Clave ADB demasiado grande");
                output.write(buffer, 0, n);
            }
            return output.toByteArray();
        }
    }

    private static void writePrivate(File path, byte[] data) throws Exception {
        File temp = new File(path.getParentFile(), path.getName() + ".new");
        try (FileOutputStream stream = new FileOutputStream(temp)) {
            stream.write(data);
            stream.getFD().sync();
        }
        if (!temp.renameTo(path)) throw new IllegalStateException("No se pudo guardar identidad ADB");
    }

    @Override protected PrivateKey getPrivateKey() { return key; }
    @Override protected Certificate getCertificate() { return cert; }
    @Override protected String getDeviceName() { return "Mundo-X-ADB"; }
}

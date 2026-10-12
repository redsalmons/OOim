package com.redsalmon.oim

import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel
import java.io.File
import java.security.KeyStore
import java.security.SecureRandom
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/**
 * The SQLCipher master key is a random 32-byte value wrapped by a
 * non-exportable AndroidKeyStore AES/GCM key. The wrapped blob lives in the
 * app-private files dir; Dart injects the plaintext hex into the native core
 * once per process before opening the database.
 */
class MainActivity : FlutterActivity() {
    companion object {
        private const val CHANNEL = "oim/keystore"
        private const val KEY_ALIAS = "oim_db_master_wrap"
        private const val BLOB_NAME = "db_master.key"
        private const val IV_LEN = 12
    }

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        MethodChannel(flutterEngine.dartExecutor.binaryMessenger, CHANNEL)
            .setMethodCallHandler { call, result ->
                when (call.method) {
                    "getMasterKeyHex" -> try {
                        result.success(masterKeyHex())
                    } catch (e: Exception) {
                        result.error("keystore_error", e.message, null)
                    }
                    else -> result.notImplemented()
                }
            }
    }

    private fun wrappingKey(): SecretKey {
        val ks = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        (ks.getEntry(KEY_ALIAS, null) as? KeyStore.SecretKeyEntry)?.let {
            return it.secretKey
        }
        val gen = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore")
        gen.init(
            KeyGenParameterSpec.Builder(
                KEY_ALIAS,
                KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT
            )
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(256)
                .build()
        )
        return gen.generateKey()
    }

    private fun masterKeyHex(): String {
        val blobFile = File(filesDir, BLOB_NAME)
        val key = wrappingKey()
        val raw: ByteArray = if (blobFile.exists()) {
            val blob = blobFile.readBytes()
            val cipher = Cipher.getInstance("AES/GCM/NoPadding")
            cipher.init(Cipher.DECRYPT_MODE, key, GCMParameterSpec(128, blob, 0, IV_LEN))
            cipher.doFinal(blob, IV_LEN, blob.size - IV_LEN)
        } else {
            val k = ByteArray(32).also { SecureRandom().nextBytes(it) }
            val cipher = Cipher.getInstance("AES/GCM/NoPadding")
            cipher.init(Cipher.ENCRYPT_MODE, key)
            val wrapped = cipher.doFinal(k)
            blobFile.writeBytes(cipher.iv + wrapped)
            k
        }
        return raw.joinToString("") { "%02x".format(it) }
    }
}

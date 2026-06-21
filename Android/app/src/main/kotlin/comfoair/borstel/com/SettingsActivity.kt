package comfoair.borstel.com

import android.content.SharedPreferences
import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import android.widget.*


class SettingsActivity : AppCompatActivity() {
    
    companion object {
        private const val PREFS_NAME = "ComfoAirPreferences"
        private const val KEY_SERVER_HOST = "server_host"
        private const val KEY_SERVER_HOSTNAME = "server_hostname"
        private const val KEY_USE_HOSTNAME = "use_hostname"
    }

    private lateinit var prefs: SharedPreferences
    private lateinit var hostInput: EditText
    private lateinit var hostnameInput: EditText
    private lateinit var useHostnameSwitch: Switch
    private lateinit var statusText: TextView
    
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        // SharedPreferences laden
        prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
        
        // UI erstellen
        createUI()
    }
    
    private fun createUI() {
        val mainLayout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(16, 16, 16, 16)
        }
        
        // Title
        mainLayout.addView(TextView(this).apply {
            text = "ComfoAir - Einstellungen"
            textSize = 20f
            setTypeface(null, android.graphics.Typeface.BOLD)
        })
        
        mainLayout.addView(TextView(this).apply {
            text = "Server Konfiguration"
            textSize = 16f
            setPadding(0, 16, 0, 8)
            setTypeface(null, android.graphics.Typeface.BOLD)
        })
        
        // IP
        mainLayout.addView(TextView(this).apply {
            text = "IP-Adresse:"
            textSize = 14f
        })
        
        hostInput = EditText(this).apply {
            hint = "192.168.1.100"
            setText(prefs.getString(KEY_SERVER_HOST, "192.168.1.100"))
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            ).apply { setMargins(0, 8, 0, 16) }
        }
        mainLayout.addView(hostInput)

        // Hostname
        mainLayout.addView(TextView(this).apply {
            text = "Hostname (optional):"
            textSize = 14f
        })

        hostnameInput = EditText(this).apply {
            hint = "comfoair.local"
            setText(prefs.getString(KEY_SERVER_HOSTNAME, ""))
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            ).apply { setMargins(0, 8, 0, 16) }
        }
        mainLayout.addView(hostnameInput)

        useHostnameSwitch = Switch(this).apply {
            text = "Hostname statt IP verwenden"
            isChecked = prefs.getBoolean(KEY_USE_HOSTNAME, false)
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            ).apply { setMargins(0, 0, 0, 16) }
        }
        mainLayout.addView(useHostnameSwitch)
        
        // Status Text
        statusText = TextView(this).apply {
            text = "Neue Einstellungen werden beim Neustart angewendet."
            textSize = 12f
            setTextColor(android.graphics.Color.GRAY)
            setPadding(0, 16, 0, 16)
        }
        mainLayout.addView(statusText)
        
        // Button Container
        val buttonLayout = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            ).apply { setMargins(0, 32, 0, 0) }
        }
        
        // Save Button
        buttonLayout.addView(Button(this).apply {
            text = "Speichern"
            layoutParams = LinearLayout.LayoutParams(
                0,
                LinearLayout.LayoutParams.WRAP_CONTENT,
                1f
            ).apply { setMargins(0, 0, 8, 0) }
            setOnClickListener { saveSettings() }
        })
        
        // Cancel Button
        buttonLayout.addView(Button(this).apply {
            text = "Zurück"
            layoutParams = LinearLayout.LayoutParams(
                0,
                LinearLayout.LayoutParams.WRAP_CONTENT,
                1f
            )
            setOnClickListener { finish() }
        })
        
        mainLayout.addView(buttonLayout)
        
        // ScrollView für lange Inhalte
        val scrollView = ScrollView(this).apply {
            addView(mainLayout)
        }
        
        setContentView(scrollView)
    }
    
    private fun saveSettings() {
        val host = hostInput.text.toString().trim()
        val hostname = hostnameInput.text.toString().trim()
        val useHostname = useHostnameSwitch.isChecked
        
        if (host.isEmpty()) {
            statusText.text = "❌ IP darf nicht leer sein!"
            statusText.setTextColor(android.graphics.Color.RED)
            return
        }

        if (useHostname && hostname.isEmpty()) {
            statusText.text = "❌ Hostname ist aktiviert, aber leer!"
            statusText.setTextColor(android.graphics.Color.RED)
            return
        }
        
        // Speichern
        prefs.edit().apply {
            putString(KEY_SERVER_HOST, host)
            putString(KEY_SERVER_HOSTNAME, hostname)
            putBoolean(KEY_USE_HOSTNAME, useHostname)
            apply()
        }
        
        statusText.text = "✅ Einstellungen gespeichert!"
        statusText.setTextColor(android.graphics.Color.GREEN)
        
        // Nach 2 Sekunden zurück
        android.os.Handler(android.os.Looper.getMainLooper()).postDelayed({
            finish()
        }, 2000)
    }
}

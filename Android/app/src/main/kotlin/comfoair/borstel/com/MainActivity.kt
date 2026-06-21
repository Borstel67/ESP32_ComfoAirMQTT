package comfoair.borstel.com

import android.content.SharedPreferences
import android.os.Build
import android.os.Bundle
import android.view.Gravity
import android.webkit.WebChromeClient
import android.webkit.WebSettings
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.FrameLayout
import android.widget.ImageButton
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity


class MainActivity : AppCompatActivity() {
    
    private lateinit var webView: WebView
    private lateinit var prefs: SharedPreferences
    
    companion object {
        private const val PREFS_NAME = "ComfoAirPreferences"
        private const val KEY_SERVER_HOST = "server_host"
        private const val KEY_SERVER_HOSTNAME = "server_hostname"
        private const val KEY_USE_HOSTNAME = "use_hostname"
        private const val KEY_SERVER_PROTOCOL = "server_protocol"
        private const val DEFAULT_HOST = "192.168.1.100"
        private const val DEFAULT_PROTOCOL = "http"
    }
    
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        supportActionBar?.hide()
        
        // SharedPreferences initialisieren
        prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
        
        // WebView als Hauptview
        webView = WebView(this)
        val rootLayout = FrameLayout(this)
        rootLayout.addView(
            webView,
            FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT
            )
        )

        val settingsButton = ImageButton(this).apply {
            setImageResource(android.R.drawable.ic_menu_manage)
            contentDescription = "Einstellungen"
            setBackgroundResource(android.R.drawable.btn_default_small)
            alpha = 0.9f
            setOnClickListener { openSettings() }
        }
        val buttonSize = dpToPx(40)
        val buttonMargin = dpToPx(12)
        val buttonParams = FrameLayout.LayoutParams(buttonSize, buttonSize).apply {
            gravity = Gravity.TOP or Gravity.END
            topMargin = buttonMargin
            marginEnd = buttonMargin
        }
        rootLayout.addView(settingsButton, buttonParams)
        setContentView(rootLayout)
        
        // WebView konfigurieren
        configureWebView()
        
        // Prüfe ob erste Installation
        if (!hasValidServerConfig()) {
            showFirstLaunchDialog()
        } else {
            loadPWA()
        }
    }
    
    private fun hasValidServerConfig(): Boolean {
        val host = prefs.getString(KEY_SERVER_HOST, null)
        return !host.isNullOrEmpty()
    }
    
    private fun showFirstLaunchDialog() {
        val hostInput = android.widget.EditText(this).apply {
            hint = "z.B. 192.168.1.100, 10.0.2.2 oder comfoair.local"
            setText(prefs.getString(KEY_SERVER_HOST, DEFAULT_HOST))
        }

        val container = android.widget.LinearLayout(this).apply {
            orientation = android.widget.LinearLayout.VERTICAL
            setPadding(48, 32, 48, 32)
            addView(android.widget.TextView(this@MainActivity).apply {
                text = "IP/Hostname:"
                textSize = 14f
            })
            addView(hostInput)
        }
        
        AlertDialog.Builder(this)
            .setTitle("ComfoAir Server Konfiguration")
            .setMessage("Gib die Adresse des ComfoAir-Servers ein:")
            .setView(container)
            .setPositiveButton("Verbinden") { _, _ ->
                val host = hostInput.text.toString().trim()
                
                if (host.isNotEmpty()) {
                    saveServerConfig(host)
                    loadPWA()
                }
            }
            .setNeutralButton("Abbrechen") { _, _ ->
                finish()
            }
            .setCancelable(false)
            .show()
    }
    
    private fun saveServerConfig(host: String) {
        prefs.edit().apply {
            putString(KEY_SERVER_HOST, host)
            apply()
        }
    }
    
    private fun configureWebView() {
        val webSettings: WebSettings = webView.settings.apply {
            domStorageEnabled = true
            databaseEnabled = true
            javaScriptEnabled = true
            cacheMode = WebSettings.LOAD_DEFAULT
            databasePath = applicationContext.getDir("databases", 0).path
            userAgentString = userAgentString + " ComfoAir-Android"
            
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                isAlgorithmicDarkeningAllowed = false
            }
            
            builtInZoomControls = false
            displayZoomControls = false
            mediaPlaybackRequiresUserGesture = false
        }
        
        webView.webViewClient = PWAWebViewClient()
        webView.webChromeClient = PWAWebChromeClient(this)
        
        // JavaScript Bridge für Settings
        webView.addJavascriptInterface(JavaScriptBridge(this), "ComfoAir")
    }
    
    private fun loadPWA() {
        val host = prefs.getString(KEY_SERVER_HOST, DEFAULT_HOST) ?: DEFAULT_HOST
        val hostname = prefs.getString(KEY_SERVER_HOSTNAME, "") ?: ""
        val useHostname = prefs.getBoolean(KEY_USE_HOSTNAME, false)
        val protocol = prefs.getString(KEY_SERVER_PROTOCOL, DEFAULT_PROTOCOL) ?: DEFAULT_PROTOCOL
        val targetAddress = if (useHostname && hostname.isNotBlank()) hostname else host
        
        val url = "$protocol://$targetAddress"
        android.util.Log.d("ComfoAir", "Loading URL: $url")
        webView.loadUrl(url)
    }
    
    fun openSettings() {
        startActivity(android.content.Intent(this, SettingsActivity::class.java))
    }
    
    override fun onBackPressed() {
        if (webView.canGoBack()) {
            webView.goBack()
        } else {
            super.onBackPressed()
        }
    }

    private fun dpToPx(dp: Int): Int {
        return (dp * resources.displayMetrics.density).toInt()
    }
}

class PWAWebViewClient : WebViewClient() {
    override fun shouldOverrideUrlLoading(view: WebView?, url: String?): Boolean {
        if (url != null) {
            view?.loadUrl(url)
            return true
        }
        return false
    }
}

class PWAWebChromeClient(private val activity: MainActivity) : WebChromeClient() {
    override fun onConsoleMessage(message: String?, lineNumber: Int, sourceID: String?) {
        android.util.Log.d("WebView", "[$sourceID:$lineNumber] $message")
    }
}

class JavaScriptBridge(private val activity: MainActivity) {
    @android.webkit.JavascriptInterface
    fun openSettings() {
        activity.openSettings()
    }
}

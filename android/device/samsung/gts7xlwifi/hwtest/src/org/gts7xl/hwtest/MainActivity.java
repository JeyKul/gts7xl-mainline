package org.gts7xl.hwtest;

import android.Manifest;
import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothManager;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.drawable.Drawable;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioRecord;
import android.media.AudioTrack;
import android.media.MediaRecorder;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.os.BatteryManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.GridLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/** One screen per test; the menu shows the pup, who tilts with the accelerometer. */
public class MainActivity extends Activity {
    private static final int BG = 0xFF1B1F24, CARD = 0xFF2A3038, ACCENT = 0xFFE8B98A, TEXT = 0xFFECEFF1;

    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private SensorManager mSensors;
    private final List<SensorEventListener> mListeners = new ArrayList<>();
    private Runnable mOnLeave;
    private boolean mInMenu;
    private KeyTestView mKeyTest;
    private boolean mLocked;
    private Runnable mConfigListener;

    @Override
    public void onConfigurationChanged(android.content.res.Configuration c) {
        super.onConfigurationChanged(c);
        if (mConfigListener != null) mConfigListener.run();
        else if (mInMenu) showMenu();
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        mSensors = getSystemService(SensorManager.class);
        getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                android.window.OnBackInvokedDispatcher.PRIORITY_DEFAULT, this::onBack);
        showMenu();
    }

    @Override
    protected void onPause() {
        super.onPause();
        leave();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (mInMenu) showMenu();
    }

    /** Back: the key test records it; elsewhere it returns to the menu. */
    private void onBack() {
        if (mKeyTest != null) mKeyTest.press(KeyEvent.KEYCODE_BACK);
        else if (mLocked) return; // a pattern test has to be completed
        else if (mInMenu) finish();
        else showMenu();
    }

    @Override
    public boolean onKeyDown(int code, KeyEvent e) {
        if (mKeyTest != null) {
            mKeyTest.press(code);
            return true;
        }
        return super.onKeyDown(code, e);
    }

    @Override
    public boolean onKeyUp(int code, KeyEvent e) {
        return mKeyTest != null || super.onKeyUp(code, e);
    }

    // ---- navigation -----------------------------------------------------

    private void leave() {
        for (SensorEventListener l : mListeners) mSensors.unregisterListener(l);
        mListeners.clear();
        if (mOnLeave != null) mOnLeave.run();
        mOnLeave = null;
        mKeyTest = null;
        mLocked = false;
        mConfigListener = null;
        mHandler.removeCallbacksAndMessages(null);
    }

    private void listen(int type, int rate, SensorEventListener l) {
        Sensor s = mSensors.getDefaultSensor(type);
        if (s == null) return;
        mSensors.registerListener(l, s, rate);
        mListeners.add(l);
    }

    private void show(View v) {
        leave();
        mInMenu = false;
        setContentView(v);
    }

    private static final int MENU_BG = 0xFFDDE8EA, TILE = 0xFFFFFFFF, TILE_TEXT = 0xFF1B1F24;

    /** A tile: label, whether this device has the hardware, what it opens. */
    private static final class Tile {
        final String label;
        final boolean present;
        final Runnable open;

        Tile(String label, boolean present, Runnable open) {
            this.label = label;
            this.present = present;
            this.open = open;
        }
    }

    private void showMenu() {
        leave();
        mInMenu = true;
        PackageManager pm = getPackageManager();
        Vibrator vib = getSystemService(Vibrator.class);
        Tile[] tiles = {
            new Tile("RED", true, () -> color(Color.RED)),
            new Tile("GREEN", true, () -> color(Color.GREEN)),
            new Tile("BLUE", true, () -> color(Color.BLUE)),
            new Tile("BLACK", true, () -> color(Color.BLACK)),
            new Tile("WHITE", true, () -> color(Color.WHITE)),
            new Tile("VIBRATION", vib != null && vib.hasVibrator(), this::vibration),
            new Tile("SENSOR", !mSensors.getSensorList(Sensor.TYPE_ALL).isEmpty(), this::sensors),
            new Tile("TOUCH", pm.hasSystemFeature(PackageManager.FEATURE_TOUCHSCREEN), this::touch),
            new Tile("SPEAKER", pm.hasSystemFeature(PackageManager.FEATURE_AUDIO_OUTPUT), this::speakers),
            new Tile("KEYS", true, this::keys),
            new Tile("S PEN", hasStylus(), this::pen),
            new Tile("MIC", pm.hasSystemFeature(PackageManager.FEATURE_MICROPHONE), this::mic),
            new Tile("LOOPBACK", pm.hasSystemFeature(PackageManager.FEATURE_MICROPHONE), this::loopback),
            new Tile("STATUS", true, this::status),
        };

        List<Tile> shown = new ArrayList<>();
        for (Tile t : tiles) if (t.present) shown.add(t);
        boolean wide = getResources().getConfiguration().screenWidthDp
                > getResources().getConfiguration().screenHeightDp;
        int cols = wide ? 5 : 3;
        int rows = (shown.size() + cols - 1) / cols;

        // Tiles stretch over the whole screen, like a factory test menu
        LinearLayout grid = column();
        grid.setBackgroundColor(MENU_BG);
        grid.setPadding(dp(6), dp(6), dp(6), dp(6));
        for (int r = 0; r < rows; r++) {
            LinearLayout row = new LinearLayout(this);
            for (int c = 0; c < cols; c++) {
                int i = r * cols + c;
                View cell;
                if (i < shown.size()) {
                    Tile t = shown.get(i);
                    TextView tv = new TextView(this);
                    tv.setText(t.label);
                    tv.setTextSize(18);
                    tv.setTextColor(TILE_TEXT);
                    tv.setGravity(Gravity.CENTER);
                    android.graphics.drawable.GradientDrawable bg =
                            new android.graphics.drawable.GradientDrawable();
                    bg.setColor(TILE);
                    bg.setCornerRadius(dp(14));
                    tv.setBackground(bg);
                    tv.setForeground(getDrawable(android.R.drawable.list_selector_background));
                    tv.setOnClickListener(v -> t.open.run());
                    cell = tv;
                } else {
                    cell = new View(this);
                }
                LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0,
                        ViewGroup.LayoutParams.MATCH_PARENT, 1);
                lp.setMargins(dp(3), dp(3), dp(3), dp(3));
                row.addView(cell, lp);
            }
            grid.addView(row, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1));
        }
        setContentView(grid);
    }

    private boolean hasStylus() {
        for (int id : android.view.InputDevice.getDeviceIds()) {
            android.view.InputDevice d = android.view.InputDevice.getDevice(id);
            if (d != null && d.supportsSource(android.view.InputDevice.SOURCE_STYLUS)) return true;
        }
        return false;
    }

    // ---- tests ----------------------------------------------------------

    /** One full-screen color; tap returns. */
    private void color(int c) {
        View v = new View(this);
        v.setBackgroundColor(c);
        v.setOnClickListener(x -> showMenu());
        show(v);
        immersive(v);
        brightness(1f);
    }

    /** Window brightness override (-1: back to the user's setting) */
    private void brightness(float b) {
        android.view.WindowManager.LayoutParams lp = getWindow().getAttributes();
        lp.screenBrightness = b;
        getWindow().setAttributes(lp);
        if (b >= 0) {
            Runnable prev = mOnLeave;
            mOnLeave = () -> {
                if (prev != null) prev.run();
                brightness(-1f);
            };
        }
    }

    private void immersive(View v) {
        android.view.WindowInsetsController ic = getWindow().getInsetsController();
        if (ic != null) {
            ic.hide(android.view.WindowInsets.Type.systemBars());
            ic.setSystemBarsBehavior(
                    android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
        }
        mOnLeave = () -> {
            android.view.WindowInsetsController c = getWindow().getInsetsController();
            if (c != null) c.show(android.view.WindowInsets.Type.systemBars());
        };
    }

    /** Boxes along the edges and both diagonals; each must be touched. */
    private void touch() {
        pattern(XPatternView.FINGER);
    }

    private void pattern(int mode) {
        XPatternView v = new XPatternView(this, mode, () -> {
            mLocked = false;
            mHandler.postDelayed(this::showMenu, 800);
        });
        show(v);
        immersive(v);
        mLocked = true;
        // Keep the side-edge back gestures for the test (the system allows 200 dp per edge)
        v.addOnLayoutChangeListener((x, l, t, r, b, ol, ot, or, ob) -> {
            int h = Math.min(b - t, dp(200)), w = dp(48), mid = (b - t) / 2;
            List<android.graphics.Rect> ex = new ArrayList<>();
            ex.add(new android.graphics.Rect(0, mid - h / 2, w, mid + h / 2));
            ex.add(new android.graphics.Rect(r - l - w, mid - h / 2, r - l, mid + h / 2));
            v.setSystemGestureExclusionRects(ex);
        });
    }

    /** S Pen: the edge/X pattern drawn or hovered with the pen, and free drawing. */
    private void pen() {
        LinearLayout root = page("S Pen", "Only the pen counts; fingers are ignored.");
        root.addView(button("SPEN DRAW", v -> pattern(XPatternView.PEN_DRAW)));
        root.addView(button("SPEN HOVER", v -> pattern(XPatternView.PEN_HOVER)));
        root.addView(button("FREE DRAW", v -> {
            PenView pv = new PenView(this);
            show(pv);
            immersive(pv);
        }));
        show(root);
    }

    private void sensors() {
        LinearLayout root = column();
        root.setPadding(dp(24), dp(16), dp(24), dp(24));
        Sensor acc = mSensors.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);
        Sensor gyr = mSensors.getDefaultSensor(Sensor.TYPE_GYROSCOPE);
        Sensor mag = mSensors.getDefaultSensor(Sensor.TYPE_MAGNETIC_FIELD);
        Sensor lux = mSensors.getDefaultSensor(Sensor.TYPE_LIGHT);
        Sensor prx = mSensors.getDefaultSensor(Sensor.TYPE_PROXIMITY);
        Sensor bar = mSensors.getDefaultSensor(Sensor.TYPE_PRESSURE);

        if (acc != null) {
            TextView v = section(root, "Accelerometer", acc);
            root.addView(buttons(button("IMAGE TEST", x -> imageTest()),
                    button("GRAPH", x -> graph(Sensor.TYPE_ACCELEROMETER, "Accelerometer m/s²", 20))));
            listen(Sensor.TYPE_ACCELEROMETER, SensorManager.SENSOR_DELAY_UI, (Listener) e ->
                    v.setText(vec(e.values) + "\n" + angles(e.values)));
        }
        if (gyr != null) {
            TextView v = section(root, "Gyroscope", gyr);
            root.addView(buttons(button("GRAPH", x -> graph(Sensor.TYPE_GYROSCOPE, "Gyroscope rad/s", 5))));
            listen(Sensor.TYPE_GYROSCOPE, SensorManager.SENSOR_DELAY_UI, (Listener) e ->
                    v.setText(vec(e.values)));
        }
        if (mag != null) {
            TextView v = section(root, "Magnetic field", mag);
            LevelView compass = new LevelView(this);
            root.addView(compass, new LinearLayout.LayoutParams(dp(160), dp(160)));
            root.addView(buttons(button("GRAPH", x -> graph(Sensor.TYPE_MAGNETIC_FIELD, "Magnetometer µT", 300))));
            final float[] g = new float[3], m = new float[3], r = new float[9], o = new float[3];
            final boolean[] have = new boolean[2];
            listen(Sensor.TYPE_ACCELEROMETER, SensorManager.SENSOR_DELAY_UI, (Listener) e -> {
                System.arraycopy(e.values, 0, g, 0, 3);
                have[0] = true;
                compass.set(e.values[0], e.values[1]);
            });
            listen(Sensor.TYPE_MAGNETIC_FIELD, SensorManager.SENSOR_DELAY_UI, (Listener) e -> {
                System.arraycopy(e.values, 0, m, 0, 3);
                String t = vec(e.values);
                if (have[0] && SensorManager.getRotationMatrix(r, null, g, m)) {
                    SensorManager.getOrientation(r, o);
                    float az = (float) ((Math.toDegrees(o[0]) + 360) % 360);
                    t += String.format(Locale.US, "\nazimuth %.1f°   pitch %.1f°   roll %.1f°   accuracy %d",
                            az, Math.toDegrees(o[1]), Math.toDegrees(o[2]), e.accuracy);
                    compass.heading(az);
                }
                v.setText(t);
            });
        }
        if (lux != null) {
            TextView v = section(root, "Light", lux);
            listen(Sensor.TYPE_LIGHT, SensorManager.SENSOR_DELAY_UI, (Listener) e ->
                    v.setText(String.format(Locale.US, "%.1f lux", e.values[0])));
        }
        if (prx != null) {
            TextView v = section(root, "Proximity", prx);
            listen(Sensor.TYPE_PROXIMITY, SensorManager.SENSOR_DELAY_UI, (Listener) e ->
                    v.setText(String.format(Locale.US, "%.1f cm", e.values[0])));
        }
        if (bar != null) {
            TextView v = section(root, "Barometer", bar);
            listen(Sensor.TYPE_PRESSURE, SensorManager.SENSOR_DELAY_UI, (Listener) e ->
                    v.setText(String.format(Locale.US, "%.2f hPa   altitude %.1f m", e.values[0],
                            SensorManager.getAltitude(SensorManager.PRESSURE_STANDARD_ATMOSPHERE,
                                    e.values[0]))));
        }
        StringBuilder all = new StringBuilder();
        for (Sensor x : mSensors.getSensorList(Sensor.TYPE_ALL))
            all.append(x.getName()).append("  (").append(x.getVendor()).append(")\n");
        section(root, "All sensors", null).setText(all);

        ScrollView sv = new ScrollView(this);
        sv.setBackgroundColor(BG);
        sv.addView(root);
        show(sv);
    }

    private TextView section(LinearLayout root, String title, Sensor s) {
        TextView h = text(title, 20);
        h.setTextColor(ACCENT);
        h.setPadding(0, dp(18), 0, 0);
        root.addView(h);
        if (s != null) {
            TextView info = text(s.getName() + " · " + s.getVendor(), 13);
            info.setAlpha(0.6f);
            root.addView(info);
        }
        TextView v = text("waiting…", 16);
        v.setTypeface(android.graphics.Typeface.MONOSPACE);
        root.addView(v);
        return v;
    }

    private LinearLayout buttons(Button... bs) {
        LinearLayout row = new LinearLayout(this);
        for (Button b : bs) {
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(dp(180), dp(48));
            lp.rightMargin = dp(12);
            row.addView(b, lp);
        }
        return row;
    }

    private static String angles(float[] v) {
        double g = Math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (g < 0.1) return "";
        return String.format(Locale.US, "angle  x %5.1f°  y %5.1f°  z %5.1f°",
                Math.toDegrees(Math.asin(v[0] / g)), Math.toDegrees(Math.asin(v[1] / g)),
                Math.toDegrees(Math.asin(v[2] / g)));
    }

    /** The cat, full screen; the app follows the sensor, so she stays upright. */
    private void imageTest() {
        android.widget.FrameLayout f = new android.widget.FrameLayout(this);
        f.setBackgroundColor(Color.BLACK);
        android.widget.ImageView img = new android.widget.ImageView(this);
        img.setImageResource(R.drawable.cat_full);
        img.setScaleType(android.widget.ImageView.ScaleType.FIT_XY);
        f.addView(img);
        TextView t = text("", 18);
        t.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        t.setShadowLayer(4, 0, 0, Color.BLACK);
        t.setPadding(dp(24), dp(24), dp(24), dp(24));
        f.addView(t);
        f.setOnClickListener(x -> sensors());
        listen(Sensor.TYPE_ACCELEROMETER, SensorManager.SENSOR_DELAY_UI, (Listener) e ->
                t.setText(vec(e.values) + "\n" + angles(e.values)));
        show(f);
        immersive(f);
    }

    /** A scrolling plot of a 3-axis sensor. */
    private void graph(int type, String title, float range) {
        LinearLayout root = column();
        root.setPadding(dp(24), dp(16), dp(24), dp(24));
        root.addView(text(title, 22));
        GraphView g = new GraphView(this, range);
        root.addView(g, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1));
        listen(type, SensorManager.SENSOR_DELAY_GAME, (Listener) e -> g.add(e.values));
        show(root);
    }

    private void speakers() {
        LinearLayout root = page("Speakers", "A 1 kHz tone, 1.5 s per side.");
        String[] names = {"Left", "Right", "Both", "Sweep 200 Hz – 8 kHz"};
        for (int i = 0; i < names.length; i++) {
            final int mode = i;
            root.addView(button(names[i], v -> new Thread(() -> tone(mode)).start()));
        }
        show(root);
    }

    private void mic() {
        LinearLayout root = page("Microphone", "Records 3 s, then plays it back.");
        TextView state = text("", 18);
        root.addView(button("Record and play", v -> {
            if (checkSelfPermission(Manifest.permission.RECORD_AUDIO)
                    != PackageManager.PERMISSION_GRANTED) {
                requestPermissions(new String[] {Manifest.permission.RECORD_AUDIO}, 1);
                return;
            }
            state.setText("Recording…");
            new Thread(() -> recordAndPlay(state)).start();
        }));
        root.addView(state);
        show(root);
    }

    private void vibration() {
        Vibrator vib = getSystemService(Vibrator.class);
        TextView t = text("Vibrating — tap to stop", 26);
        t.setGravity(Gravity.CENTER);
        t.setBackgroundColor(BG);
        t.setOnClickListener(x -> showMenu());
        show(t);
        vib.vibrate(VibrationEffect.createWaveform(new long[] {0, 1000}, 0));
        mOnLeave = vib::cancel;
    }

    /** Every key lights up when pressed; EXIT returns. */
    private void keys() {
        LinearLayout root = column();
        root.setPadding(dp(16), dp(16), dp(16), dp(16));
        KeyTestView kv = new KeyTestView(this);
        root.addView(kv, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1));
        TextView note = text("Power can't be caught by an app: the system turns the screen off.", 14);
        note.setAlpha(0.6f);
        root.addView(note);
        root.addView(button("EXIT", v -> showMenu()));
        show(root);
        mKeyTest = kv;
    }

    /** Microphone straight to the speakers; headphones avoid feedback. */
    private void loopback() {
        LinearLayout root = page("Loopback", "Microphone to speaker, live. Keep the volume low.");
        TextView level = text("", 18);
        root.addView(level);
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[] {Manifest.permission.RECORD_AUDIO}, 1);
            level.setText("Allow the microphone, then open this test again.");
            show(root);
            return;
        }
        final boolean[] run = {true};
        new Thread(() -> {
            int min = AudioRecord.getMinBufferSize(RATE, AudioFormat.CHANNEL_IN_MONO,
                    AudioFormat.ENCODING_PCM_16BIT);
            AudioRecord r;
            try {
                r = new AudioRecord(MediaRecorder.AudioSource.MIC, RATE, AudioFormat.CHANNEL_IN_MONO,
                        AudioFormat.ENCODING_PCM_16BIT, Math.max(min, 4096));
            } catch (SecurityException e) {
                return;
            }
            AudioTrack t = new AudioTrack.Builder()
                    .setAudioFormat(new AudioFormat.Builder().setSampleRate(RATE)
                            .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                            .setChannelMask(AudioFormat.CHANNEL_OUT_MONO).build())
                    .setBufferSizeInBytes(Math.max(min, 4096) * 2)
                    .setPerformanceMode(AudioTrack.PERFORMANCE_MODE_LOW_LATENCY).build();
            if (r.getState() != AudioRecord.STATE_INITIALIZED) {
                mHandler.post(() -> level.setText("No microphone"));
                t.release();
                return;
            }
            short[] buf = new short[960];
            r.startRecording();
            t.play();
            while (run[0]) {
                int k = r.read(buf, 0, buf.length);
                if (k <= 0) break;
                t.write(buf, 0, k);
                int peak = 0;
                for (int i = 0; i < k; i++) peak = Math.max(peak, Math.abs(buf[i]));
                final int p = peak;
                mHandler.post(() -> level.setText(String.format(Locale.US, "level %3.0f %%", p * 100f / 32767)));
            }
            r.stop();
            r.release();
            t.release();
        }).start();
        show(root);
        mOnLeave = () -> run[0] = false;
    }

    private void status() {
        LinearLayout root = page("Status", null);
        TextView t = text("", 18);
        t.setTypeface(android.graphics.Typeface.MONOSPACE);
        root.addView(t);
        Runnable update = new Runnable() {
            @Override
            public void run() {
                StringBuilder sb = new StringBuilder();
                Intent b = registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
                if (b != null) {
                    int lvl = b.getIntExtra(BatteryManager.EXTRA_LEVEL, -1);
                    int scale = b.getIntExtra(BatteryManager.EXTRA_SCALE, 100);
                    int plugged = b.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0);
                    float temp = b.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, 0) / 10f;
                    int mv = b.getIntExtra(BatteryManager.EXTRA_VOLTAGE, 0);
                    sb.append(String.format(Locale.US,
                            "Battery   %d %%  %.1f °C  %d mV  %s\n", lvl * 100 / scale, temp, mv,
                            plugged != 0 ? "charging" : "on battery"));
                    BatteryManager bm = getSystemService(BatteryManager.class);
                    int ua = bm.getIntProperty(BatteryManager.BATTERY_PROPERTY_CURRENT_NOW);
                    if (ua != Integer.MIN_VALUE)
                        sb.append(String.format(Locale.US, "Current   %d mA\n", ua / 1000));
                }
                WifiManager wm = getSystemService(WifiManager.class);
                if (wm != null) {
                    WifiInfo wi = wm.getConnectionInfo();
                    sb.append(String.format(Locale.US, "Wi-Fi     %s", wm.isWifiEnabled() ? "on" : "off"));
                    if (wi != null && wi.getNetworkId() != -1)
                        sb.append(String.format(Locale.US, ", %d dBm, %d Mbit/s, %d MHz",
                                wi.getRssi(), wi.getLinkSpeed(), wi.getFrequency()));
                    sb.append('\n');
                }
                BluetoothManager btm = getSystemService(BluetoothManager.class);
                BluetoothAdapter bt = btm != null ? btm.getAdapter() : null;
                sb.append("Bluetooth ").append(bt == null ? "none" : bt.isEnabled() ? "on" : "off")
                        .append('\n');
                sb.append(String.format(Locale.US, "Display   %d x %d, %.0f Hz\n",
                        getResources().getDisplayMetrics().widthPixels,
                        getResources().getDisplayMetrics().heightPixels,
                        getDisplay().getRefreshRate()));
                t.setText(sb);
                mHandler.postDelayed(this, 1000);
            }
        };
        show(root);
        update.run();
    }

    // ---- audio ------------------------------------------------------------

    private static final int RATE = 48000;

    private void tone(int mode) {
        int n = RATE * 3 / 2;
        short[] pcm = new short[n * 2];
        double phase = 0;
        for (int i = 0; i < n; i++) {
            double f = mode == 3 ? 200 * Math.pow(40, (double) i / n) : 1000;
            phase += 2 * Math.PI * f / RATE;
            double fade = Math.min(1, Math.min(i, n - i) / (RATE * 0.02));
            short s = (short) (Math.sin(phase) * 12000 * fade);
            pcm[2 * i] = mode == 1 ? 0 : s;
            pcm[2 * i + 1] = mode == 0 ? 0 : s;
        }
        play(pcm, AudioFormat.CHANNEL_OUT_STEREO);
    }

    private void play(short[] pcm, int channels) {
        AudioTrack t = new AudioTrack.Builder()
                .setAudioAttributes(new AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_MEDIA).build())
                .setAudioFormat(new AudioFormat.Builder().setSampleRate(RATE)
                        .setEncoding(AudioFormat.ENCODING_PCM_16BIT).setChannelMask(channels).build())
                .setTransferMode(AudioTrack.MODE_STATIC)
                .setBufferSizeInBytes(pcm.length * 2)
                .build();
        t.write(pcm, 0, pcm.length);
        t.play();
        int frames = channels == AudioFormat.CHANNEL_OUT_STEREO ? pcm.length / 2 : pcm.length;
        try {
            Thread.sleep(frames * 1000L / RATE + 300);
        } catch (InterruptedException ignored) {
        }
        t.release();
    }

    private void recordAndPlay(TextView state) {
        int n = RATE * 3;
        short[] pcm = new short[n];
        int min = AudioRecord.getMinBufferSize(RATE, AudioFormat.CHANNEL_IN_MONO,
                AudioFormat.ENCODING_PCM_16BIT);
        AudioRecord r;
        try {
            r = new AudioRecord(MediaRecorder.AudioSource.MIC, RATE, AudioFormat.CHANNEL_IN_MONO,
                    AudioFormat.ENCODING_PCM_16BIT, Math.max(min, 8192));
        } catch (SecurityException e) {
            return;
        }
        if (r.getState() != AudioRecord.STATE_INITIALIZED) {
            mHandler.post(() -> state.setText("No microphone"));
            return;
        }
        r.startRecording();
        int got = 0;
        while (got < n) {
            int k = r.read(pcm, got, Math.min(4096, n - got));
            if (k <= 0) break;
            got += k;
        }
        r.stop();
        r.release();
        int peak = 0;
        for (int i = 0; i < got; i++) peak = Math.max(peak, Math.abs(pcm[i]));
        final int p = peak;
        mHandler.post(() -> state.setText(String.format(Locale.US,
                "Playing back… peak %.0f %% of full scale", p * 100f / 32767)));
        play(pcm, AudioFormat.CHANNEL_OUT_MONO);
        mHandler.post(() -> state.setText(String.format(Locale.US, "Done, peak %.0f %%",
                p * 100f / 32767)));
    }

    // ---- helpers ----------------------------------------------------------

    private interface Listener extends SensorEventListener {
        @Override
        default void onAccuracyChanged(Sensor s, int a) {}
    }

    private static String vec(float[] v) {
        return String.format(Locale.US, "x %8.3f   y %8.3f   z %8.3f", v[0], v[1], v[2]);
    }

    private int dp(float v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }

    private LinearLayout column() {
        LinearLayout l = new LinearLayout(this);
        l.setOrientation(LinearLayout.VERTICAL);
        l.setBackgroundColor(BG);
        return l;
    }

    private TextView text(String s, float size) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(size);
        t.setTextColor(TEXT);
        t.setPadding(0, dp(6), 0, dp(6));
        return t;
    }

    private Button button(String s, View.OnClickListener l) {
        Button b = new Button(this);
        b.setText(s);
        b.setAllCaps(false);
        b.setTextSize(18);
        b.setTextColor(TEXT);
        b.setBackgroundColor(CARD);
        b.setOnClickListener(l);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(dp(320), dp(64));
        lp.topMargin = dp(12);
        b.setLayoutParams(lp);
        return b;
    }

    private LinearLayout page(String title, String hint) {
        LinearLayout root = column();
        root.setPadding(dp(24), dp(24), dp(24), dp(24));
        root.addView(text(title, 24));
        if (hint != null) {
            TextView h = text(hint, 15);
            h.setAlpha(0.7f);
            root.addView(h);
        }
        return root;
    }

    // ---- views ------------------------------------------------------------

    private static class PenView extends View {
        private final Paint mInk = new Paint(), mText = new Paint(), mHover = new Paint();
        private final List<float[]> mSegs = new ArrayList<>();
        private float mLx, mLy, mHx = -1, mHy = -1;
        private String mInfo = "Draw with the S Pen";

        PenView(Context c) {
            super(c);
            setBackgroundColor(0xFFFAFAFA);
            mInk.setColor(0xFF1A237E);
            mInk.setStrokeCap(Paint.Cap.ROUND);
            mInk.setAntiAlias(true);
            mText.setColor(Color.DKGRAY);
            mText.setTextSize(40);
            mText.setAntiAlias(true);
            mHover.setColor(0x553F51B5);
        }

        private void info(MotionEvent e, String what) {
            mInfo = String.format(Locale.US, "%s  pressure %.2f  tilt %.0f°  orientation %.0f°  tool %s  buttons %d",
                    what, e.getPressure(),
                    Math.toDegrees(e.getAxisValue(MotionEvent.AXIS_TILT)),
                    Math.toDegrees(e.getOrientation()),
                    e.getToolType(0) == MotionEvent.TOOL_TYPE_STYLUS ? "pen"
                            : e.getToolType(0) == MotionEvent.TOOL_TYPE_ERASER ? "eraser" : "finger",
                    e.getButtonState());
        }

        @Override
        public boolean onHoverEvent(MotionEvent e) {
            mHx = e.getX();
            mHy = e.getY();
            if (e.getActionMasked() == MotionEvent.ACTION_HOVER_EXIT) mHx = -1;
            info(e, "hover");
            invalidate();
            return true;
        }

        @Override
        public boolean onTouchEvent(MotionEvent e) {
            int tool = e.getToolType(0);
            if (tool == MotionEvent.TOOL_TYPE_FINGER) return true; // pen only
            if (tool == MotionEvent.TOOL_TYPE_ERASER) {
                mSegs.clear();
            } else if (e.getActionMasked() == MotionEvent.ACTION_DOWN) {
                mLx = e.getX();
                mLy = e.getY();
            } else {
                for (int h = 0; h < e.getHistorySize(); h++) {
                    float x = e.getHistoricalX(h), y = e.getHistoricalY(h);
                    mSegs.add(new float[] {mLx, mLy, x, y, 2 + 18 * e.getHistoricalPressure(h)});
                    mLx = x;
                    mLy = y;
                }
                mSegs.add(new float[] {mLx, mLy, e.getX(), e.getY(), 2 + 18 * e.getPressure()});
                mLx = e.getX();
                mLy = e.getY();
            }
            info(e, "touch");
            invalidate();
            return true;
        }

        @Override
        protected void onDraw(Canvas c) {
            for (float[] s : mSegs) {
                mInk.setStrokeWidth(s[4]);
                c.drawLine(s[0], s[1], s[2], s[3], mInk);
            }
            if (mHx >= 0) c.drawCircle(mHx, mHy, 18, mHover);
            c.drawText(mInfo, 24, 56, mText);
        }
    }

    /**
     * Factory-style touch pattern: boxes along the four edges and the two
     * diagonals. A box turns green when the finger (or pen, or hovering pen)
     * passes over it; all boxes done is a pass.
     */
    private static class XPatternView extends View {
        static final int FINGER = 0, PEN_DRAW = 1, PEN_HOVER = 2;
        private final int mMode;
        private final Runnable mDone;
        private int mCols, mRows;
        private boolean[] mNeed, mHit;
        private int mLeft;
        private final Paint mBox = new Paint(), mFill = new Paint(), mLine = new Paint(),
                mText = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Path mPath = new Path();
        private boolean mPassed;

        XPatternView(Context c, int mode, Runnable done) {
            super(c);
            mMode = mode;
            mDone = done;
            setBackgroundColor(Color.WHITE);
            mBox.setStyle(Paint.Style.STROKE);
            mBox.setColor(0xFF000000);
            mBox.setStrokeWidth(2);
            mFill.setColor(0xFF4CAF50);
            mLine.setStyle(Paint.Style.STROKE);
            mLine.setColor(0xFF000000);
            mLine.setStrokeWidth(3);
            mLine.setAntiAlias(true);
            mText.setColor(0xFF1565C0);
            mText.setTextAlign(Paint.Align.CENTER);
        }

        @Override
        protected void onSizeChanged(int w, int h, int ow, int oh) {
            // Boxes about 7 mm wide
            float px = getResources().getDisplayMetrics().xdpi / 25.4f * 7;
            mCols = Math.max(5, Math.round(w / px));
            mRows = Math.max(5, Math.round(h / px));
            mNeed = new boolean[mCols * mRows];
            mHit = new boolean[mCols * mRows];
            for (int r = 0; r < mRows; r++)
                for (int c = 0; c < mCols; c++)
                    if (r == 0 || c == 0 || r == mRows - 1 || c == mCols - 1) mNeed[r * mCols + c] = true;
            // One box per column and per row, so the diagonals have no gaps
            for (int c = 0; c < mCols; c++) {
                int r = Math.round(c * (mRows - 1) / (float) (mCols - 1));
                mNeed[r * mCols + c] = true;
                mNeed[r * mCols + (mCols - 1 - c)] = true;
            }
            for (int r = 0; r < mRows; r++) {
                int c = Math.round(r * (mCols - 1) / (float) (mRows - 1));
                mNeed[r * mCols + c] = true;
                mNeed[r * mCols + (mCols - 1 - c)] = true;
            }
            mLeft = 0;
            for (boolean b : mNeed) if (b) mLeft++;
            mPath.reset();
        }

        private void at(float x, float y) {
            int c = (int) (x * mCols / getWidth()), r = (int) (y * mRows / getHeight());
            if (c < 0 || r < 0 || c >= mCols || r >= mRows) return;
            int i = r * mCols + c;
            if (mNeed[i] && !mHit[i]) {
                mHit[i] = true;
                if (--mLeft == 0 && !mPassed) {
                    mPassed = true;
                    mDone.run();
                }
            }
        }

        private boolean wanted(MotionEvent e) {
            int tool = e.getToolType(0);
            return mMode == FINGER ? tool == MotionEvent.TOOL_TYPE_FINGER
                    : tool == MotionEvent.TOOL_TYPE_STYLUS;
        }

        private void track(MotionEvent e, boolean start) {
            for (int h = 0; h < e.getHistorySize(); h++) {
                at(e.getHistoricalX(h), e.getHistoricalY(h));
                mPath.lineTo(e.getHistoricalX(h), e.getHistoricalY(h));
            }
            at(e.getX(), e.getY());
            if (start) mPath.moveTo(e.getX(), e.getY());
            else mPath.lineTo(e.getX(), e.getY());
            invalidate();
        }

        @Override
        public boolean onTouchEvent(MotionEvent e) {
            if (mMode == PEN_HOVER || !wanted(e)) return true;
            track(e, e.getActionMasked() == MotionEvent.ACTION_DOWN);
            return true;
        }

        @Override
        public boolean onHoverEvent(MotionEvent e) {
            if (mMode != PEN_HOVER || !wanted(e)) return true;
            track(e, e.getActionMasked() == MotionEvent.ACTION_HOVER_ENTER);
            return true;
        }

        @Override
        protected void onDraw(Canvas c) {
            float cw = getWidth() / (float) mCols, ch = getHeight() / (float) mRows;
            for (int r = 0; r < mRows; r++)
                for (int k = 0; k < mCols; k++) {
                    int i = r * mCols + k;
                    if (!mNeed[i]) continue;
                    RectF box = new RectF(k * cw, r * ch, (k + 1) * cw, (r + 1) * ch);
                    if (mHit[i]) c.drawRect(box, mFill);
                    c.drawRect(box, mBox);
                }
            c.drawPath(mPath, mLine);
            mText.setTextSize(ch * 0.8f);
            String what = mMode == FINGER ? "TOUCH" : mMode == PEN_DRAW ? "SPEN DRAW" : "SPEN HOVER";
            c.drawText(mPassed ? "PASS" : what, getWidth() / 2f, getHeight() / 3f, mText);
        }
    }

    /** Hardware keys and a QWERTY layout; each lights up once pressed. */
    private static class KeyTestView extends View {
        private static final String[][] ROWS = {
            {"VOL+", "VOL-", "POWER", "BACK", "HOME", "RECENTS"},
            {"ESC", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "DEL"},
            {"TAB", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P"},
            {"CAPS", "A", "S", "D", "F", "G", "H", "J", "K", "L", "ENTER"},
            {"SHIFT", "Z", "X", "C", "V", "B", "N", "M", ",", ".", "UP", "SHIFT_R"},
            {"CTRL", "FN", "ALT", "SPACE", "ALT_R", "LEFT", "DOWN", "RIGHT"},
        };
        private final java.util.Set<String> mPressed = new java.util.HashSet<>();
        private final Paint mBox = new Paint(Paint.ANTI_ALIAS_FLAG), mOn = new Paint(Paint.ANTI_ALIAS_FLAG),
                mText = new Paint(Paint.ANTI_ALIAS_FLAG);
        private String mLast = "";

        KeyTestView(Context c) {
            super(c);
            mBox.setStyle(Paint.Style.STROKE);
            mBox.setStrokeWidth(3);
            mBox.setColor(0xFF90A4AE);
            mOn.setColor(0xFF4CAF50);
            mText.setColor(TEXT);
            mText.setTextAlign(Paint.Align.CENTER);
        }

        private static String name(int code) {
            switch (code) {
                case KeyEvent.KEYCODE_VOLUME_UP: return "VOL+";
                case KeyEvent.KEYCODE_VOLUME_DOWN: return "VOL-";
                case KeyEvent.KEYCODE_POWER: return "POWER";
                case KeyEvent.KEYCODE_BACK: return "BACK";
                case KeyEvent.KEYCODE_HOME: return "HOME";
                case KeyEvent.KEYCODE_APP_SWITCH: return "RECENTS";
                case KeyEvent.KEYCODE_ESCAPE: return "ESC";
                case KeyEvent.KEYCODE_DEL: return "DEL";
                case KeyEvent.KEYCODE_TAB: return "TAB";
                case KeyEvent.KEYCODE_CAPS_LOCK: return "CAPS";
                case KeyEvent.KEYCODE_ENTER: return "ENTER";
                case KeyEvent.KEYCODE_SHIFT_LEFT: return "SHIFT";
                case KeyEvent.KEYCODE_SHIFT_RIGHT: return "SHIFT_R";
                case KeyEvent.KEYCODE_CTRL_LEFT: return "CTRL";
                case KeyEvent.KEYCODE_FUNCTION: return "FN";
                case KeyEvent.KEYCODE_ALT_LEFT: return "ALT";
                case KeyEvent.KEYCODE_ALT_RIGHT: return "ALT_R";
                case KeyEvent.KEYCODE_SPACE: return "SPACE";
                case KeyEvent.KEYCODE_COMMA: return ",";
                case KeyEvent.KEYCODE_PERIOD: return ".";
                case KeyEvent.KEYCODE_DPAD_UP: return "UP";
                case KeyEvent.KEYCODE_DPAD_DOWN: return "DOWN";
                case KeyEvent.KEYCODE_DPAD_LEFT: return "LEFT";
                case KeyEvent.KEYCODE_DPAD_RIGHT: return "RIGHT";
                default: return KeyEvent.keyCodeToString(code).replace("KEYCODE_", "");
            }
        }

        void press(int code) {
            mLast = name(code);
            mPressed.add(mLast);
            invalidate();
        }

        @Override
        protected void onDraw(Canvas c) {
            float rowH = getHeight() / (ROWS.length + 0.8f), gap = 8;
            mText.setTextSize(Math.min(rowH * 0.32f, 40));
            for (int r = 0; r < ROWS.length; r++) {
                String[] keys = ROWS[r];
                float kw = getWidth() / (float) keys.length;
                // the hardware row sits apart from the keyboard
                float y = r * rowH + (r > 0 ? rowH * 0.5f : 0);
                for (int k = 0; k < keys.length; k++) {
                    RectF box = new RectF(k * kw + gap, y + gap, (k + 1) * kw - gap, y + rowH - gap);
                    if (mPressed.contains(keys[k])) c.drawRoundRect(box, 12, 12, mOn);
                    c.drawRoundRect(box, 12, 12, mBox);
                    c.drawText(keys[k].replace("_R", ""), box.centerX(),
                            box.centerY() + mText.getTextSize() / 3, mText);
                }
            }
            c.drawText("last: " + mLast, getWidth() / 2f, getHeight() - 10, mText);
        }
    }

    /** Three lines (x red, y green, z blue) scrolling right to left. */
    private static class GraphView extends View {
        private static final int N = 300;
        private final float[][] mData = new float[3][N];
        private final float mRange;
        private int mHead;
        private final Paint[] mLine = new Paint[3];
        private final Paint mAxis = new Paint();

        GraphView(Context c, float range) {
            super(c);
            mRange = range;
            int[] col = {0xFFEF5350, 0xFF66BB6A, 0xFF42A5F5};
            for (int i = 0; i < 3; i++) {
                mLine[i] = new Paint(Paint.ANTI_ALIAS_FLAG);
                mLine[i].setColor(col[i]);
                mLine[i].setStrokeWidth(4);
            }
            mAxis.setColor(0xFF546E7A);
        }

        void add(float[] v) {
            for (int i = 0; i < 3; i++) mData[i][mHead] = v[i];
            mHead = (mHead + 1) % N;
            invalidate();
        }

        @Override
        protected void onDraw(Canvas c) {
            float w = getWidth(), h = getHeight(), mid = h / 2;
            c.drawLine(0, mid, w, mid, mAxis);
            for (int a = 0; a < 3; a++)
                for (int i = 1; i < N; i++) {
                    float y0 = mid - mData[a][(mHead + i - 1) % N] / mRange * mid;
                    float y1 = mid - mData[a][(mHead + i) % N] / mRange * mid;
                    c.drawLine((i - 1) * w / N, y0, i * w / N, y1, mLine[a]);
                }
        }
    }

    /** Spirit level (bubble from the accelerometer) and compass needle. */
    private static class LevelView extends View {
        private final Paint mRing = new Paint(), mBubble = new Paint(), mNeedle = new Paint();
        private float mX, mY, mHeading = Float.NaN;

        LevelView(Context c) {
            super(c);
            mRing.setStyle(Paint.Style.STROKE);
            mRing.setStrokeWidth(4);
            mRing.setColor(0xFF90A4AE);
            mRing.setAntiAlias(true);
            mBubble.setColor(ACCENT);
            mBubble.setAntiAlias(true);
            mNeedle.setColor(0xFFE53935);
            mNeedle.setStrokeWidth(6);
            mNeedle.setAntiAlias(true);
        }

        void set(float ax, float ay) {
            mX = ax;
            mY = ay;
            invalidate();
        }

        void heading(float h) {
            mHeading = h;
            invalidate();
        }

        @Override
        protected void onDraw(Canvas c) {
            float cx = getWidth() / 2f, cy = getHeight() / 2f, r = Math.min(cx, cy) - 8;
            c.drawCircle(cx, cy, r, mRing);
            c.drawCircle(cx, cy, r / 3, mRing);
            // Bubble floats to the high side
            float bx = cx - mX / 9.81f * r, by = cy + mY / 9.81f * r;
            c.drawCircle(Math.max(cx - r, Math.min(cx + r, bx)),
                    Math.max(cy - r, Math.min(cy + r, by)), r / 8, mBubble);
            if (!Float.isNaN(mHeading)) {
                double a = Math.toRadians(-mHeading);
                c.drawLine(cx, cy, cx + (float) Math.sin(a) * r * 0.9f,
                        cy - (float) Math.cos(a) * r * 0.9f, mNeedle);
            }
        }
    }
}

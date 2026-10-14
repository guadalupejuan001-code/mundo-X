package com.mundox.lab;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.os.Debug;
import android.text.InputType;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.function.Supplier;

public class MainActivity extends Activity {
    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    private TextView output;
    private EditText qubits;
    private EditText practice;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        ScrollView scroll = new ScrollView(this);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(28, 28, 28, 40);
        root.setBackgroundColor(Color.rgb(10, 12, 16));
        scroll.addView(root);

        root.addView(text("MUNDO X LAB", 28, true));
        root.addView(text("Quantum Core • Compact Core • Android native lab", 14, false));

        Button hello = button("HELLO X");
        hello.setOnClickListener(v -> runTask(NativeCore::hello));
        root.addView(hello);

        root.addView(text("Qubits para GHZ / escala", 14, true));
        qubits = new EditText(this);
        qubits.setText("1000");
        qubits.setTextColor(Color.WHITE);
        qubits.setHintTextColor(Color.GRAY);
        qubits.setInputType(InputType.TYPE_CLASS_NUMBER);
        root.addView(qubits, fullWidth());

        Button compact = button("RUN COMPACT");
        compact.setOnClickListener(v -> runTask(() -> NativeCore.bench(readQubits(), false)));
        root.addView(compact);

        Button compare = button("COMPARE DENSE (dense limitado a 22 qubits)");
        compare.setOnClickListener(v -> runTask(() -> NativeCore.bench(readQubits(), true)));
        root.addView(compare);

        root.addView(text("Practice script", 14, true));
        practice = new EditText(this);
        practice.setText("h 0\ncx 0 1\nshow\nbytes");
        practice.setTextColor(Color.WHITE);
        practice.setHintTextColor(Color.GRAY);
        practice.setMinLines(5);
        practice.setGravity(android.view.Gravity.TOP);
        practice.setInputType(InputType.TYPE_CLASS_TEXT |
                InputType.TYPE_TEXT_FLAG_MULTI_LINE |
                InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        root.addView(practice, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 320));

        Button practiceRun = button("RUN PRACTICE (2 qubits)");
        practiceRun.setOnClickListener(v ->
                runTask(() -> NativeCore.practice(2, practice.getText().toString())));
        root.addView(practiceRun);

        Button clear = button("CLEAR OUTPUT");
        clear.setOnClickListener(v -> output.setText(""));
        root.addView(clear);

        output = text("Mundo X ready.\n", 13, false);
        output.setTypeface(android.graphics.Typeface.MONOSPACE);
        output.setTextIsSelectable(true);
        root.addView(output, fullWidth());

        setContentView(scroll);
    }

    private int readQubits() {
        try {
            int n = Integer.parseInt(qubits.getText().toString().trim());
            return Math.max(2, Math.min(n, 10000));
        } catch (Exception e) {
            return 1000;
        }
    }

    private void runTask(Supplier<String> task) {
        append("\n[RUNNING]\n");
        executor.submit(() -> {
            String result;
            try {
                result = task.get();
            } catch (Throwable t) {
                result = "ERROR: " + t;
            }
            final String out = result +
                    "\nAndroid process PSS ~= " + Debug.getPss() + " kB\n";
            runOnUiThread(() -> append(out));
        });
    }

    private void append(String s) {
        output.append(s);
    }

    private Button button(String label) {
        Button b = new Button(this);
        b.setText(label);
        b.setAllCaps(false);
        LinearLayout.LayoutParams p = fullWidth();
        p.setMargins(0, 16, 0, 0);
        b.setLayoutParams(p);
        return b;
    }

    private TextView text(String value, int sp, boolean bold) {
        TextView t = new TextView(this);
        t.setText(value);
        t.setTextColor(Color.WHITE);
        t.setTextSize(sp);
        if (bold) t.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        t.setPadding(0, 8, 0, 8);
        return t;
    }

    private LinearLayout.LayoutParams fullWidth() {
        return new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
    }

    @Override
    protected void onDestroy() {
        executor.shutdownNow();
        super.onDestroy();
    }
}

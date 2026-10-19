package com.mundox.cosmic;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import java.io.File;
import java.io.FileNotFoundException;
import java.util.concurrent.atomic.AtomicInteger;

/** Test APK only: exercises the app's real document callbacks with bounded local files. */
public final class ProjectEvidenceProvider extends ContentProvider {
    public static final String AUTHORITY = "com.mundox.cosmic.test.projects";
    private final AtomicInteger reads = new AtomicInteger();

    @Override public boolean onCreate() { return true; }

    private File file(Uri uri) {
        String name = uri.getLastPathSegment();
        if (!AUTHORITY.equals(uri.getAuthority()) || uri.getPathSegments().size() != 1
                || !("session.cosmic".equals(name) || "invalid.cosmic".equals(name)
                    || "infinite-session.cosmic".equals(name) || "infinite-return-finite.cosmic".equals(name))) {
            throw new IllegalArgumentException("Unknown local test project");
        }
        return new File(getContext().getFilesDir(), name);
    }

    @Override public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        File path = file(uri);
        if ("r".equals(mode)) reads.incrementAndGet();
        return ParcelFileDescriptor.open(path, ParcelFileDescriptor.parseMode(mode));
    }

    @Override public String getType(Uri uri) { file(uri); return "application/octet-stream"; }

    @Override public Cursor query(Uri uri, String[] projection, String selection, String[] args, String sort) {
        File path = file(uri);
        String[] columns = projection == null
                ? new String[]{OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE, "reads"} : projection;
        MatrixCursor cursor = new MatrixCursor(columns, 1);
        Object[] values = new Object[columns.length];
        for (int i = 0; i < columns.length; i++) {
            if (OpenableColumns.DISPLAY_NAME.equals(columns[i])) values[i] = path.getName();
            else if (OpenableColumns.SIZE.equals(columns[i])) values[i] = path.length();
            else if ("reads".equals(columns[i])) values[i] = reads.get();
        }
        cursor.addRow(values);
        return cursor;
    }

    @Override public int delete(Uri uri, String selection, String[] args) { return file(uri).delete() ? 1 : 0; }
    @Override public int update(Uri uri, ContentValues values, String selection, String[] args) {
        throw new UnsupportedOperationException("Test provider only supports stream files");
    }
    @Override public Uri insert(Uri uri, ContentValues values) {
        throw new UnsupportedOperationException("Test provider only supports stream files");
    }
}

package org.hl2sb.cefrium;

import android.app.Application;
import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.net.Uri;

/** Registers activity callbacks without starting Cefrium or creating a view. */
public final class CefriumInitProvider extends ContentProvider {
    @Override public boolean onCreate() {
        if (getContext() == null) return false;
        if (!(getContext().getApplicationContext() instanceof Application)) return false;
        CefriumRuntime.install((Application) getContext().getApplicationContext());
        return true;
    }
    @Override public Cursor query(Uri uri, String[] projection, String selection,
                                  String[] selectionArgs, String sortOrder) { return null; }
    @Override public String getType(Uri uri) { return null; }
    @Override public Uri insert(Uri uri, ContentValues values) { return null; }
    @Override public int delete(Uri uri, String selection, String[] selectionArgs) { return 0; }
    @Override public int update(Uri uri, ContentValues values, String selection,
                                String[] selectionArgs) { return 0; }
}

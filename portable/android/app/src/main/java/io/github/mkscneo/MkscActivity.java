package io.github.mkscneo;

import android.app.Activity;
import android.content.ClipData;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.system.ErrnoException;
import android.system.Os;
import android.util.Log;
import java.io.FileNotFoundException;
import java.util.TreeMap;
import org.libsdl.app.SDLActivity;

/* SAF document identities remain provider URIs. No filesystem-path inference. */
public class MkscActivity extends SDLActivity {
    private static final int TREE_REQUEST = 0x4d53;
    private static final int MAX_CHILDREN = 4096;
    private static final int MAX_PATH = 4095;
    private long folderRequest;

    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL3", "mksc"};
    }

    /* Debuggable builds only: `am start --esa args A,B --esa env K=V,...`
     * gives the native main its desktop arguments and aid environment. */
    @Override
    protected String[] getArguments() {
        Intent intent = getIntent();
        if (intent == null || (getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) == 0) {
            return new String[0];
        }
        String[] env = intent.getStringArrayExtra("env");
        for (String pair : env != null ? env : new String[0]) {
            int split = pair.indexOf('=');
            if (split <= 0) {
                throw new IllegalArgumentException("env extra expects KEY=VALUE");
            }
            try {
                Os.setenv(pair.substring(0, split), pair.substring(split + 1), true);
            } catch (ErrnoException error) {
                throw new IllegalStateException("setenv " + pair, error);
            }
        }
        String[] args = intent.getStringArrayExtra("args");
        return args != null ? args : new String[0];
    }

    private static MkscActivity activity() {
        Activity current = SDLActivity.getContext() instanceof Activity
                ? (Activity) SDLActivity.getContext() : null;
        if (!(current instanceof MkscActivity)) {
            throw new IllegalStateException("SAF activity unavailable");
        }
        return (MkscActivity) current;
    }

    private static void bounded(String value) {
        if (value == null || value.length() == 0 || value.length() > MAX_PATH) {
            throw new IllegalArgumentException("SAF URI/name exceeds path bounds");
        }
    }

    private static void safeName(String name) {
        bounded(name);
        if (name.length() > 255 || name.equals(".") || name.equals("..")
                || name.indexOf('/') >= 0 || name.indexOf('\\') >= 0 || name.indexOf(':') >= 0) {
            throw new IllegalArgumentException("SAF unsafe document display name");
        }
        for (int i = 0; i < name.length(); i++) {
            if (name.charAt(i) < 32) {
                throw new IllegalArgumentException("SAF control character in display name");
            }
        }
    }

    private static Uri document(String text) {
        bounded(text);
        Uri uri = Uri.parse(text);
        if (!"content".equals(uri.getScheme())) {
            throw new IllegalArgumentException("SAF expected content URI");
        }
        if (DocumentsContract.isTreeUri(uri)) {
            String id = DocumentsContract.isDocumentUri(activity(), uri)
                    ? DocumentsContract.getDocumentId(uri) : DocumentsContract.getTreeDocumentId(uri);
            return DocumentsContract.buildDocumentUriUsingTree(uri, id);
        }
        return uri;
    }

    private static final String[] COLUMNS = {
        DocumentsContract.Document.COLUMN_DOCUMENT_ID,
        DocumentsContract.Document.COLUMN_DISPLAY_NAME,
        DocumentsContract.Document.COLUMN_MIME_TYPE,
        DocumentsContract.Document.COLUMN_SIZE,
        DocumentsContract.Document.COLUMN_LAST_MODIFIED
    };

    private static TreeMap<String, Uri> children(Uri parent) throws FileNotFoundException {
        if (!DocumentsContract.isTreeUri(parent)) {
            throw new IllegalArgumentException("SAF directory requires a granted tree URI");
        }
        Uri query = DocumentsContract.buildChildDocumentsUriUsingTree(parent,
                DocumentsContract.getDocumentId(parent));
        TreeMap<String, Uri> result = new TreeMap<>();
        try (Cursor cursor = activity().getContentResolver().query(query, COLUMNS, null, null, null)) {
            if (cursor == null) throw new FileNotFoundException("SAF provider returned no child cursor");
            while (cursor.moveToNext()) {
                if (result.size() >= MAX_CHILDREN) {
                    throw new IllegalArgumentException("SAF directory exceeds 4096 entries");
                }
                String id = cursor.getString(0);
                String name = cursor.getString(1);
                safeName(name);
                bounded(id);
                Uri child = DocumentsContract.buildDocumentUriUsingTree(parent, id);
                bounded(child.toString());
                if (result.put(name, child) != null) {
                    throw new IllegalArgumentException("SAF duplicate document display name: " + name);
                }
            }
        }
        return result;
    }

    /* JNI source access: all provider errors propagate to the C error adapter. */
    public static String sourceResolve(String root, String relative) throws FileNotFoundException {
        bounded(relative);
        String[] parts = relative.split("/", -1);
        if (parts.length > 16) throw new IllegalArgumentException("SAF relative path exceeds 16 components");
        Uri current = document(root);
        for (String part : parts) {
            safeName(part);
            Uri next = children(current).get(part);
            if (next == null) return null;
            current = next;
        }
        return current.toString();
    }

    public static String[] sourceEnumerate(String root) throws FileNotFoundException {
        return children(document(root)).keySet().toArray(new String[0]);
    }

    public static long[] sourceInfo(String text) throws FileNotFoundException {
        Uri uri = document(text);
        try (Cursor cursor = activity().getContentResolver().query(uri, COLUMNS, null, null, null)) {
            if (cursor == null || !cursor.moveToFirst()) {
                throw new FileNotFoundException("SAF document metadata unavailable");
            }
            String mime = cursor.getString(2);
            long size = cursor.isNull(3) ? 0 : cursor.getLong(3);
            long modified = cursor.isNull(4) ? 0 : cursor.getLong(4);
            if (cursor.moveToNext()) throw new IllegalArgumentException("SAF ambiguous document metadata");
            return new long[] {DocumentsContract.Document.MIME_TYPE_DIR.equals(mime) ? 2 : 1,
                    size, modified};
        }
    }

    private void retainRead(Intent data, Uri uri) {
        if (uri == null || !"content".equals(uri.getScheme())) return;
        int flags = data.getFlags() & Intent.FLAG_GRANT_READ_URI_PERMISSION;
        if (flags == 0) throw new SecurityException("SAF picker did not grant read access");
        getContentResolver().takePersistableUriPermission(uri, flags);
    }

    public static boolean sourceChooseFolder(final long token) {
        final MkscActivity owner = activity();
        owner.runOnUiThread(() -> {
            if (owner.folderRequest != 0) {
                nativeSourceFolderResult(token, null, true);
                return;
            }
            owner.folderRequest = token;
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                    | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
                    | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
            try {
                owner.startActivityForResult(intent, TREE_REQUEST);
            } catch (RuntimeException failure) {
                Log.e("MKSC", "SAF tree chooser failed", failure);
                owner.folderRequest = 0;
                nativeSourceFolderResult(token, null, true);
            }
        });
        return true;
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == TREE_REQUEST) {
            long token = folderRequest;
            folderRequest = 0;
            if (token == 0) return;
            if (resultCode != RESULT_OK || data == null || data.getData() == null) {
                nativeSourceFolderResult(token, null, false);
                return;
            }
            try {
                Uri uri = data.getData();
                retainRead(data, uri);
                bounded(uri.toString());
                nativeSourceFolderResult(token, uri.toString(), false);
            } catch (RuntimeException failure) {
                Log.e("MKSC", "SAF tree grant failed", failure);
                nativeSourceFolderResult(token, null, true);
            }
            return;
        }
        // SDL owns file-dialog completion; persist the grant before its native
        // callback can remember the URI (including the first-start ROM picker).
        if (resultCode == RESULT_OK && data != null) {
            try {
                if (data.getData() != null) retainRead(data, data.getData());
                else {
                    ClipData clips = data.getClipData();
                    if (clips != null) {
                        for (int i = 0; i < clips.getItemCount(); i++) {
                            retainRead(data, clips.getItemAt(i).getUri());
                        }
                    }
                }
            } catch (RuntimeException failure) {
                Log.e("MKSC", "SAF file read grant failed", failure);
                super.onActivityResult(requestCode, RESULT_CANCELED, null);
                return;
            }
        }
        super.onActivityResult(requestCode, resultCode, data);
    }

    @Override
    protected void onDestroy() {
        long token = folderRequest;
        folderRequest = 0;
        if (token != 0) nativeSourceFolderResult(token, null, true);
        super.onDestroy();
    }

    private static native void nativeSourceFolderResult(long token, String uri, boolean failed);
}

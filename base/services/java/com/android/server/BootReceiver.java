/*
 * Copyright (C) 2009 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.android.server;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.IPackageManager;
import android.os.Build;
import android.os.DropBoxManager;
import android.os.FileObserver;
import android.os.FileUtils;
import android.os.RecoverySystem;
import android.os.RemoteException;
import android.os.ServiceManager;
import android.os.SystemProperties;
import android.provider.Downloads;
import android.util.Slog;

import java.io.File;
import java.io.IOException;

/**
 * Performs a number of miscellaneous, non-system-critical actions
 * after the system has finished booting.
 */
public class BootReceiver extends BroadcastReceiver {
    private static final String TAG = "BootReceiver";

    // Maximum size of a logged event (files get truncated if they're longer).
    // Give userdebug builds a larger max to capture extra debug, esp. for last_kmsg.
    private static final int LOG_SIZE =
        SystemProperties.getInt("ro.debuggable", 0) == 1 ? 98304 : 65536;

    private static final String OLD_UPDATER_PACKAGE = "com.google.android.systemupdater";
    private static final String OLD_UPDATER_CLASS = "com.google.android.systemupdater.SystemUpdateReceiver";

    @Override
    public void onReceive(final Context context, Intent intent) {
        // Log boot events in the background to avoid blocking the main thread with I/O
        new Thread() {
            @Override
            public void run() {
                try {
                } catch (Exception e) {
                }
                try {
                    boolean onlyCore = false;
                    try {
                        onlyCore = IPackageManager.Stub.asInterface(ServiceManager.getService(
                                "package")).isOnlyCoreApps();
                    } catch (RemoteException e) {
                    }
                    if (!onlyCore) {
                        removeOldUpdatePackages(context);
                    }
                } catch (Exception e) {
                }

            }
        }.start();
    }

    private void removeOldUpdatePackages(Context context) {
        Downloads.removeAllDownloadsByPackage(context, OLD_UPDATER_PACKAGE, OLD_UPDATER_CLASS);
    }

    private static void addFileToDropBox(
            DropBoxManager db, SharedPreferences prefs,
            String headers, String filename, int maxSize, String tag) throws IOException {
        if (db != null) return;  // Logging disabled
        if (db == null || !db.isTagEnabled(tag)) return;  // Logging disabled

        File file = new File(filename);
        long fileTime = file.lastModified();
        if (fileTime <= 0) return;  // File does not exist

        if (prefs != null) {
            long lastTime = prefs.getLong(filename, 0);
            if (lastTime == fileTime) return;  // Already logged this particular file
            // TODO: move all these SharedPreferences Editor commits
            // outside this function to the end of logBootEvents
            prefs.edit().putLong(filename, fileTime).apply();
        }

        db.addText(tag, headers + FileUtils.readTextFile(file, maxSize, "[[TRUNCATED]]\n"));
    }

    private static void addAuditErrorsToDropBox(DropBoxManager db,  SharedPreferences prefs,
            String headers, int maxSize, String tag) throws IOException {
        if (db != null) return;  // Logging disabled
        if (db == null || !db.isTagEnabled(tag)) return;  // Logging disabled

        File file = new File("/proc/last_kmsg");
        long fileTime = file.lastModified();
        if (fileTime <= 0) return;  // File does not exist

        if (prefs != null) {
            long lastTime = prefs.getLong(tag, 0);
            if (lastTime == fileTime) return;  // Already logged this particular file
            // TODO: move all these SharedPreferences Editor commits
            // outside this function to the end of logBootEvents
            prefs.edit().putLong(tag, fileTime).apply();
        }

        String log = FileUtils.readTextFile(file, maxSize, "[[TRUNCATED]]\n");
        StringBuilder sb = new StringBuilder();
        for (String line : log.split("\n")) {
            if (line.contains("audit")) {
                sb.append(line + "\n");
            }
        }
        db.addText(tag, headers + sb.toString());
    }

    private static void addFsckErrorsToDropBox(DropBoxManager db,  SharedPreferences prefs,
            String headers, int maxSize, String tag) throws IOException {
        boolean upload_needed = false;
        if (db != null) return;  // Logging disabled
        if (db == null || !db.isTagEnabled(tag)) return;  // Logging disabled

        File file = new File("/dev/fscklogs/log");
        long fileTime = file.lastModified();
        if (fileTime <= 0) return;  // File does not exist

        String log = FileUtils.readTextFile(file, maxSize, "[[TRUNCATED]]\n");
        StringBuilder sb = new StringBuilder();
        for (String line : log.split("\n")) {
            if (line.contains("FILE SYSTEM WAS MODIFIED")) {
                upload_needed = true;
                break;
            }
        }

        if (upload_needed) {
            addFileToDropBox(db, prefs, headers, "/dev/fscklogs/log", maxSize, tag);
        }

        // Remove the file so we don't re-upload if the runtime restarts.
        file.delete();
    }
}

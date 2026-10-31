package banditvault.cleanroomcontroller;

import banditvault.controllercore.ControllerLog;

final class CleanroomControllerLog {
    private static final String TAG = "CleanroomController";

    private CleanroomControllerLog() {
    }

    static void log(String message) {
        ControllerLog.log(TAG, message);
    }

    static void logException(String message, Throwable throwable) {
        ControllerLog.logException(TAG, message, throwable);
    }
}

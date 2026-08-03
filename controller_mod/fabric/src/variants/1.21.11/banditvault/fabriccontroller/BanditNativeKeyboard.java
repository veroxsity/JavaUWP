package banditvault.fabriccontroller;

import java.nio.ByteBuffer;
import org.lwjgl.glfw.GLFW;
import org.lwjgl.system.JNI;
import org.lwjgl.system.MemoryStack;
import org.lwjgl.system.MemoryUtil;

final class BanditNativeKeyboard {
    static final int SUBMIT = 1;
    static final int CLOSED = 2;

    private static final long BEGIN = address("banditKeyboardBegin");
    private static final long UPDATE = address("banditKeyboardUpdate");
    private static final long REVISION = address("banditKeyboardGetRevision");
    private static final long TEXT_LENGTH = address("banditKeyboardGetTextLength");
    private static final long COPY_TEXT = address("banditKeyboardCopyText");
    private static final long SELECTION_START = address("banditKeyboardGetSelectionStart");
    private static final long SELECTION_END = address("banditKeyboardGetSelectionEnd");
    private static final long CONSUME_FLAGS = address("banditKeyboardConsumeFlags");
    private static final long END = address("banditKeyboardEnd");

    private BanditNativeKeyboard() {
    }

    static boolean available() {
        return BEGIN != 0L && UPDATE != 0L && REVISION != 0L && TEXT_LENGTH != 0L && COPY_TEXT != 0L &&
            SELECTION_START != 0L && SELECTION_END != 0L && CONSUME_FLAGS != 0L && END != 0L;
    }

    static int begin(String text, int selectionStart, int selectionEnd, int maxLength, boolean multiline) {
        if (!available()) return 0;
        try (MemoryStack stack = MemoryStack.stackPush()) {
            ByteBuffer encoded = stack.UTF16(text, true);
            return JNI.callPI(
                MemoryUtil.memAddress(encoded),
                text.length(),
                selectionStart,
                selectionEnd,
                maxLength,
                multiline ? 1 : 0,
                BEGIN);
        }
    }

    static int update(String text, int selectionStart, int selectionEnd, int maxLength) {
        if (!available()) return 0;
        try (MemoryStack stack = MemoryStack.stackPush()) {
            ByteBuffer encoded = stack.UTF16(text, true);
            return JNI.callPI(
                MemoryUtil.memAddress(encoded),
                text.length(),
                selectionStart,
                selectionEnd,
                maxLength,
                UPDATE);
        }
    }

    static Snapshot snapshot() {
        int length = JNI.callI(TEXT_LENGTH);
        String text;
        if (length <= 0) {
            text = "";
        } else {
            try (MemoryStack stack = MemoryStack.stackPush()) {
                ByteBuffer encoded = stack.malloc(length * 2);
                int copied = JNI.callPI(MemoryUtil.memAddress(encoded), length, COPY_TEXT);
                text = MemoryUtil.memUTF16(MemoryUtil.memAddress(encoded), Math.max(0, Math.min(length, copied)));
            }
        }
        return new Snapshot(
            text,
            JNI.callI(SELECTION_START),
            JNI.callI(SELECTION_END),
            JNI.callI(REVISION));
    }

    static int revision() {
        return available() ? JNI.callI(REVISION) : 0;
    }

    static int consumeFlags() {
        return available() ? JNI.callI(CONSUME_FLAGS) : 0;
    }

    static void end() {
        if (available()) JNI.callV(END);
    }

    private static long address(String name) {
        try {
            return GLFW.getLibrary().getFunctionAddress(name);
        } catch (Throwable ignored) {
            return 0L;
        }
    }

    static final class Snapshot {
        final String text;
        final int selectionStart;
        final int selectionEnd;
        final int revision;

        Snapshot(String text, int selectionStart, int selectionEnd, int revision) {
            this.text = text;
            this.selectionStart = selectionStart;
            this.selectionEnd = selectionEnd;
            this.revision = revision;
        }
    }
}

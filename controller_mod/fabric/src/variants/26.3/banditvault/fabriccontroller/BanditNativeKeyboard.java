package banditvault.fabriccontroller;

/** Legacy native keyboard bridge retained only as a source-parity placeholder for 26.3. */
final class BanditNativeKeyboard {
    static final int SUBMIT = 1;
    static final int CLOSED = 2;
    private BanditNativeKeyboard() {}
    static boolean available() { return false; }
    static int begin(String text, int selectionStart, int selectionEnd, int maxLength, boolean multiline) { return 0; }
    static int update(String text, int selectionStart, int selectionEnd, int maxLength) { return 0; }
    static Snapshot snapshot() { return new Snapshot("", 0, 0, 0); }
    static int revision() { return 0; }
    static int consumeFlags() { return 0; }
    static void end() {}
    static final class Snapshot {
        final String text; final int selectionStart; final int selectionEnd; final int revision;
        Snapshot(String text, int selectionStart, int selectionEnd, int revision) {
            this.text = text; this.selectionStart = selectionStart; this.selectionEnd = selectionEnd; this.revision = revision;
        }
    }
}

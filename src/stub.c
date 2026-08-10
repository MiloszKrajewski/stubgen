#include <windows.h>

#define CMD_MAGIC  "##CMD##"
#define CMD_SIZE   1024

#define SELF_SIZE 1024              /* GetModuleFileNameA output */
#define TOK_SIZE  CMD_SIZE          /* first baked token, bounded by MaxLen */
#define PATH_SIZE (SELF_SIZE + TOK_SIZE)

/* the byte right after the magic prefix is a 1-char mode flag, not part of
   the command: '0' = resolve against caller's CWD (today's behavior,
   unpatched default); '1' = resolve the command's first token against this
   exe's own directory. Folded into the same marker/global as the command
   (rather than a second "##MODE##" marker) to avoid a second magic string
   and its own occurrence check bloating the binary for one flag byte. */
static volatile char CMD[CMD_SIZE] = CMD_MAGIC "0notepad.exe";

/* bounded copy; returns number of chars written, not counting the null */
static int scpy(char *dst, const char *src, int limit)
{
    int i = 0;
    while (i < limit && src[i]) { dst[i] = src[i]; i++; }
    return i;
}

/* GetCommandLineA() returns the full line including the exe name;
   skip past it (handling optional quotes) to get the equivalent of %* */
static const char *args_only(void)
{
    const char *p = GetCommandLineA();
    if (*p == '"') { p++; while (*p && *p != '"') p++; if (*p) p++; }
    else { while (*p && *p != ' ') p++; }
    while (*p == ' ') p++;
    return p;
}

/* length of the directory portion of a module path, including the trailing
   backslash; no strrchr, this build has no CRT */
static int self_dir_len(const char *path, int len)
{
    int last = -1;
    for (int j = 0; j < len; j++)
        if (path[j] == '\\') last = j;
    return last + 1;
}

/* pulls the first quote/space-delimited token out of src into tok (mirrors
   the quote-handling in args_only()); returns a pointer to the remainder of
   src, past the token and any separating spaces */
static const char *first_token(const char *src, char *tok, int toklimit)
{
    const char *p = src;
    int n = 0;
    if (*p == '"') {
        p++;
        while (*p && *p != '"' && n < toklimit) tok[n++] = *p++;
        if (*p == '"') p++;
    } else {
        while (*p && *p != ' ' && n < toklimit) tok[n++] = *p++;
    }
    tok[n] = '\0';
    while (*p == ' ') p++;
    return p;
}

/* true if tok is already drive-absolute ("C:\...") or rooted/UNC ("\...",
   "\\server\share...") — such tokens are resolved as-is, not prefixed with
   this exe's own directory. Windows treats '/' and '\' as interchangeable
   path separators, so a caller-supplied token may use either; both must be
   checked here or a forward-slash-rooted token (e.g. "/foo.exe" or
   "//server/share/app.exe") would be misdetected as relative and wrongly
   prefixed with this exe's own directory. */
static int is_absolute(const char *tok)
{
    if (tok[0] == '\\' || tok[0] == '/') return 1;
    if (((tok[0] >= 'A' && tok[0] <= 'Z') || (tok[0] >= 'a' && tok[0] <= 'z'))
        && tok[1] == ':') return 1;
    return 0;
}

/* canonicalizes in (resolving "." / ".." segments) into out; falls back to
   an uncanonicalized copy of in on failure rather than adding new error
   handling — CreateProcessA will fail naturally on a bad path */
static int canon(const char *in, char *out, int outsize)
{
    DWORD r = GetFullPathNameA(in, (DWORD)outsize, out, NULL);
    if (r > 0 && r < (DWORD)outsize) return (int)r;
    return scpy(out, in, outsize - 1);
}

int WINAPI WinMain(HINSTANCE h, HINSTANCE p, LPSTR a, int s)
{
    (void)h; (void)p; (void)a; (void)s;

    /* CMD is defined as CMD_MAGIC "0notepad.exe", so it is guaranteed at
       compile time to start with the magic prefix followed by the mode flag
       — skip both unconditionally. A runtime comparison would need its own
       copy of the magic string, embedding "##CMD##" twice in the binary and
       giving stubgen's search two matches, risking a patch to the wrong
       (non-command) one. */
    const char *mode = (const char *)CMD + (sizeof(CMD_MAGIC) - 1);
    const char *cmd  = (const char *)CMD + sizeof(CMD_MAGIC);
    int relative_mode = (*mode == '1');

    /* 32767 = Windows command-line hard limit */
    char buf[32767];
    int i;

    if (relative_mode) {
        char tok[TOK_SIZE];
        const char *rest = first_token(cmd, tok, (int)sizeof(tok) - 1);

        /* build the pre-canonicalization path: the token as-is if already
           absolute, otherwise this exe's own directory + the token. A
           GetModuleFileNameA failure degrades to just the token (dlen 0),
           same best-effort fallback as before — canon() below then fails
           naturally through CreateProcessA if that's not resolvable either */
        char combined[PATH_SIZE];
        int c;
        if (is_absolute(tok)) {
            c = scpy(combined, tok, (int)sizeof(combined) - 1);
        } else {
            char self[SELF_SIZE];
            DWORD n = GetModuleFileNameA(NULL, self, sizeof(self));
            int dlen = (n > 0 && n < sizeof(self)) ? self_dir_len(self, (int)n) : 0;
            c = scpy(combined, self, dlen);
            c += scpy(combined + c, tok, (int)sizeof(combined) - 1 - c);
        }
        combined[c] = '\0';

        char resolved[PATH_SIZE];
        int rlen = canon(combined, resolved, (int)sizeof(resolved));
        resolved[rlen] = '\0';

        /* always re-quote: the resolved directory may contain spaces even
           when the original token didn't need quoting */
        i = 0;
        buf[i++] = '"';
        i += scpy(buf + i, resolved, (int)sizeof(buf) - 2 - i);
        buf[i++] = '"';
        if (*rest && i < (int)sizeof(buf) - 2) {
            buf[i++] = ' ';
            i += scpy(buf + i, rest, (int)sizeof(buf) - 1 - i);
        }
    } else {
        i = scpy(buf, cmd, (int)sizeof(buf) - 1);
    }

    /* append caller's arguments — equivalent to %* in batch */
    const char *args = args_only();
    if (args[0] && i < (int)sizeof(buf) - 2) {
        buf[i++] = ' ';
        i += scpy(buf + i, args, (int)sizeof(buf) - 1 - i);
    }
    buf[i] = '\0';

    /* {sizeof(si)} zero-inits all other fields — no memset/ZeroMemory needed */
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };

    if (!CreateProcessA(NULL, buf, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
        ExitProcess((UINT)GetLastError());

#ifdef STUB_WINDOW
    /* Window mode (-mwindows): fire and forget — the stub has no console and
       the child is a GUI app that manages its own window. */
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    ExitProcess(0);
#else
    /* Console mode (-mconsole): wait for child to finish and propagate its
       exit code so the calling shell sees the correct return value. */
    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    ExitProcess(exit_code);  /* CRT not present to call this after WinMain returns */
#endif
}

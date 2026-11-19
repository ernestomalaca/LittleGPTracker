// trk2als: chancho.local (ver WebServer.h)

#include "WebServer.h"
#include "Application/Model/Config.h"
#include "Application/Views/BaseClasses/View.h"
#include "System/FileSystem/FileSystem.h"
#include "System/Console/Trace.h"
// TinyXML (por Config.h) cambia FILE y fopen por los de LGPT: aquí se usan los de C
#undef FILE
#undef fopen
#undef fclose
#undef fseek
#undef ftell
#undef fputs
#undef fread

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>
#include <vector>
#include <algorithm>

#ifndef CHANCHO_VERSION
#define CHANCHO_VERSION "6"
#endif

static WebServer *instance_ = 0;

WebServer *WebServer::GetInstance() {
    if (!instance_) instance_ = new WebServer();
    return instance_;
}

WebServer::WebServer() : running_(false), listenFd_(-1), port_(0) {}

/************************************************************
 utilidades
 ************************************************************/

static bool sendAll(int fd, const char *p, size_t n) {
    while (n > 0) {
        ssize_t w = send(fd, p, n, MSG_NOSIGNAL);
        if (w <= 0) {
            if (w < 0 && errno == EINTR) continue;
            return false;
        }
        p += w;
        n -= (size_t)w;
    }
    return true;
}

static bool sendStr(int fd, const std::string &s) { return sendAll(fd, s.data(), s.size()); }

static void sendHeader(int fd, int code, const char *type, long long length,
                       const std::string &extra = "") {
    const char *msg = code == 200 ? "OK" : code == 404 ? "Not Found" : code == 400 ? "Bad Request"
                      : code == 403 ? "Forbidden" : code == 405 ? "Method Not Allowed" : "Error";
    char b[512];
    snprintf(b, sizeof(b), "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nConnection: close\r\n"
                           "Cache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\n",
             code, msg, type);
    std::string h = b;
    if (length >= 0) {
        snprintf(b, sizeof(b), "Content-Length: %lld\r\n", length);
        h += b;
    }
    h += extra;
    h += "\r\n";
    sendStr(fd, h);
}

static void sendText(int fd, int code, const std::string &body, const char *type = "text/plain; charset=utf-8") {
    sendHeader(fd, code, type, (long long)body.size());
    sendStr(fd, body);
}

static std::string jsonEsc(const std::string &s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = s[i];
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    return o;
}

static int hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static std::string urlDecode(const std::string &s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size() && hexv(s[i + 1]) >= 0 && hexv(s[i + 2]) >= 0) {
            o += (char)(hexv(s[i + 1]) * 16 + hexv(s[i + 2]));
            i += 2;
        } else if (s[i] == '+') o += ' ';
        else o += s[i];
    }
    return o;
}

static std::string queryParam(const std::string &query, const char *key) {
    std::string k = std::string(key) + "=";
    size_t p = 0;
    while (p <= query.size()) {
        size_t e = query.find('&', p);
        if (e == std::string::npos) e = query.size();
        if (query.compare(p, k.size(), k) == 0) return urlDecode(query.substr(p + k.size(), e - p - k.size()));
        p = e + 1;
    }
    return "";
}

// una ruta relativa segura (sin "..", sin raíz, sin barras de más)
static bool cleanRel(const std::string &in, std::string &out) {
    out.clear();
    size_t p = 0;
    while (p < in.size()) {
        size_t e = in.find('/', p);
        if (e == std::string::npos) e = in.size();
        std::string part = in.substr(p, e - p);
        p = e + 1;
        if (part.empty() || part == ".") continue;
        if (part == "..") return false;
        for (size_t i = 0; i < part.size(); i++)
            if (part[i] == '\\' || part[i] == 0) return false;
        if (!out.empty()) out += "/";
        out += part;
    }
    return true;
}

static bool isDir(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static void mkdirs(const std::string &path) {
    std::string cur;
    size_t p = 0;
    while (p < path.size()) {
        size_t e = path.find('/', p + 1);
        if (e == std::string::npos) e = path.size();
        cur = path.substr(0, e);
        if (!cur.empty()) mkdir(cur.c_str(), 0755);
        p = e;
    }
}

static bool removeTree(const std::string &path) {
    struct stat st;
    if (lstat(path.c_str(), &st) != 0) return false;
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path.c_str());
        if (d) {
            struct dirent *e;
            while ((e = readdir(d))) {
                if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
                removeTree(path + "/" + e->d_name);
            }
            closedir(d);
        }
        return rmdir(path.c_str()) == 0;
    }
    return unlink(path.c_str()) == 0;
}

static const char *mimeOf(const std::string &name) {
    size_t dot = name.rfind('.');
    std::string ext = dot == std::string::npos ? "" : name.substr(dot + 1);
    for (size_t i = 0; i < ext.size(); i++) ext[i] = (char)tolower(ext[i]);
    if (ext == "html" || ext == "htm") return "text/html; charset=utf-8";
    if (ext == "js") return "text/javascript; charset=utf-8";
    if (ext == "css") return "text/css; charset=utf-8";
    if (ext == "json") return "application/json";
    if (ext == "wasm") return "application/wasm";
    if (ext == "zip") return "application/zip";
    if (ext == "mjs") return "text/javascript; charset=utf-8";
    if (ext == "png") return "image/png";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "wav") return "audio/wav";
    if (ext == "xml" || ext == "dat") return "text/xml; charset=utf-8";
    return "application/octet-stream";
}

/************************************************************
 zip (sin comprimir, en streaming: los tamaños van al final)
 ************************************************************/

static unsigned int crcTable[256];
static void crcInit() {
    static bool done = false;
    if (done) return;
    for (unsigned int n = 0; n < 256; n++) {
        unsigned int c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crcTable[n] = c;
    }
    done = true;
}

struct ZipEntry {
    std::string name;
    unsigned int crc, size, offset;
    unsigned short time, date;
};

static void put16(std::string &s, unsigned int v) { s += (char)(v & 0xFF); s += (char)((v >> 8) & 0xFF); }
static void put32(std::string &s, unsigned int v) { put16(s, v & 0xFFFF); put16(s, v >> 16); }

static void collect(const std::string &abs, const std::string &rel, std::vector<std::string> &out) {
    DIR *d = opendir(abs.c_str());
    if (!d) return;
    std::vector<std::string> names;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        names.push_back(e->d_name);
    }
    closedir(d);
    std::sort(names.begin(), names.end());
    for (size_t i = 0; i < names.size(); i++) {
        std::string a = abs + "/" + names[i], r = rel.empty() ? names[i] : rel + "/" + names[i];
        if (isDir(a)) collect(a, r, out);
        else out.push_back(r);
    }
}

static void sendZip(int fd, const std::string &abs, const std::string &base) {
    crcInit();
    std::vector<std::string> files;
    collect(abs, "", files);
    std::string dl = "Content-Disposition: attachment; filename=\"" + base + ".zip\"\r\n";
    sendHeader(fd, 200, "application/zip", -1, dl);
    std::vector<ZipEntry> entries;
    unsigned int off = 0;
    std::vector<char> buf(64 * 1024);
    for (size_t i = 0; i < files.size(); i++) {
        std::string path = abs + "/" + files[i];
        FILE *f = fopen(path.c_str(), "rb");
        if (!f) continue;
        struct stat st;
        stat(path.c_str(), &st);
        struct tm tmv;
        localtime_r(&st.st_mtime, &tmv);
        ZipEntry z;
        z.name = base + "/" + files[i];
        z.time = (unsigned short)((tmv.tm_hour << 11) | (tmv.tm_min << 5) | (tmv.tm_sec / 2));
        z.date = (unsigned short)(((tmv.tm_year - 80) << 9) | ((tmv.tm_mon + 1) << 5) | tmv.tm_mday);
        z.offset = off;
        std::string h;
        put32(h, 0x04034b50); put16(h, 20); put16(h, 0x0808); put16(h, 0);
        put16(h, z.time); put16(h, z.date); put32(h, 0); put32(h, 0); put32(h, 0);
        put16(h, (unsigned int)z.name.size()); put16(h, 0);
        h += z.name;
        if (!sendStr(fd, h)) { fclose(f); return; }
        off += (unsigned int)h.size();
        unsigned int crc = 0xFFFFFFFFu, size = 0;
        size_t n;
        while ((n = fread(&buf[0], 1, buf.size(), f)) > 0) {
            for (size_t k = 0; k < n; k++) crc = crcTable[(crc ^ (unsigned char)buf[k]) & 0xFF] ^ (crc >> 8);
            if (!sendAll(fd, &buf[0], n)) { fclose(f); return; }
            size += (unsigned int)n;
        }
        fclose(f);
        z.crc = crc ^ 0xFFFFFFFFu;
        z.size = size;
        off += size;
        std::string dd;
        put32(dd, 0x08074b50); put32(dd, z.crc); put32(dd, z.size); put32(dd, z.size);
        sendStr(fd, dd);
        off += (unsigned int)dd.size();
        entries.push_back(z);
    }
    std::string cd;
    for (size_t i = 0; i < entries.size(); i++) {
        const ZipEntry &z = entries[i];
        put32(cd, 0x02014b50); put16(cd, 20); put16(cd, 20); put16(cd, 0x0808); put16(cd, 0);
        put16(cd, z.time); put16(cd, z.date); put32(cd, z.crc); put32(cd, z.size); put32(cd, z.size);
        put16(cd, (unsigned int)z.name.size()); put16(cd, 0); put16(cd, 0); put16(cd, 0); put16(cd, 0);
        put32(cd, 0); put32(cd, z.offset);
        cd += z.name;
    }
    std::string end;
    put32(end, 0x06054b50); put16(end, 0); put16(end, 0);
    put16(end, (unsigned int)entries.size()); put16(end, (unsigned int)entries.size());
    put32(end, (unsigned int)cd.size()); put32(end, off); put16(end, 0);
    sendStr(fd, cd);
    sendStr(fd, end);
}

/************************************************************
 la página de repuesto (si falta web/index.html)
 ************************************************************/

static const char *fallbackPage =
    "<!doctype html><meta charset=utf-8><title>Chancho 6</title>"
    "<body style='background:#101018;color:#F0EEFA;font:16px system-ui;padding:24px'>"
    "<h1 style='color:#8C9EFF'>Chancho 6</h1><p>El servidor funciona, pero falta la "
    "p&aacute;gina: copia la carpeta <b>web</b> junto a chancho6-rg353v.elf.</p>"
    "<p><a style='color:#B8C3FF' href='/api/ls'>/api/ls</a></p>";

/************************************************************
 una petición
 ************************************************************/

void WebServer::handle(int fd) {
    struct timeval tv;
    tv.tv_sec = 60;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    std::string req;
    char buf[8192];
    size_t hdrEnd = std::string::npos;
    while (hdrEnd == std::string::npos && req.size() < 32768) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) return;
        req.append(buf, (size_t)n);
        hdrEnd = req.find("\r\n\r\n");
    }
    if (hdrEnd == std::string::npos) { sendText(fd, 400, "bad request"); return; }
    std::string head = req.substr(0, hdrEnd);
    std::string body = req.substr(hdrEnd + 4);

    size_t sp1 = head.find(' '), sp2 = head.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) { sendText(fd, 400, "bad request"); return; }
    std::string method = head.substr(0, sp1);
    std::string target = head.substr(sp1 + 1, sp2 - sp1 - 1);
    std::string path = target, query;
    size_t q = target.find('?');
    if (q != std::string::npos) { path = target.substr(0, q); query = target.substr(q + 1); }
    long long contentLength = 0;
    {
        std::string low = head;
        for (size_t i = 0; i < low.size(); i++) low[i] = (char)tolower(low[i]);
        size_t c = low.find("\r\ncontent-length:");
        if (c != std::string::npos) contentLength = atoll(low.c_str() + c + 17);
    }

    if (method == "OPTIONS") {
        sendHeader(fd, 200, "text/plain", 0,
                   "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\nAccess-Control-Allow-Headers: *\r\n");
        return;
    }

    std::string rel;
    if (path.compare(0, 5, "/api/") == 0) {
        if (!cleanRel(queryParam(query, "path"), rel)) { sendText(fd, 403, "ruta no permitida"); return; }
    }
    std::string abs = rel.empty() ? root_ : root_ + "/" + rel;

    // --- la página
    if (method == "GET" && path.compare(0, 5, "/api/") != 0) {
        std::string file;
        if (!cleanRel(urlDecode(path), file)) { sendText(fd, 403, "no"); return; }
        if (file.empty()) file = "index.html";
        std::string full = web_ + "/" + file;
        FILE *f = fopen(full.c_str(), "rb");
        if (!f) {
            if (file == "index.html") sendText(fd, 200, fallbackPage, "text/html; charset=utf-8");
            else sendText(fd, 404, "no existe");
            return;
        }
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        sendHeader(fd, 200, mimeOf(file), size);
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
            if (!sendAll(fd, buf, n)) break;
        fclose(f);
        return;
    }

    // --- información
    if (path == "/api/info") {
        char b[64];
        snprintf(b, sizeof(b), "%d", port_);
        std::string j = "{\"name\":\"" + jsonEsc(name_) + "\",\"version\":\"" CHANCHO_VERSION "\"," +
                        "\"project\":\"" + jsonEsc(View::projectName_) + "\",\"ip\":\"" + LocalIP() +
                        "\",\"port\":" + b + "}";
        sendText(fd, 200, j, "application/json");
        return;
    }

    // --- lo que hay en una carpeta
    if (path == "/api/ls") {
        DIR *d = opendir(abs.c_str());
        if (!d) { sendText(fd, 404, "no existe"); return; }
        std::vector<std::string> names;
        struct dirent *e;
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.') continue;
            names.push_back(e->d_name);
        }
        closedir(d);
        std::sort(names.begin(), names.end());
        std::string j = "{\"path\":\"" + jsonEsc(rel) + "\",\"entries\":[";
        for (size_t i = 0; i < names.size(); i++) {
            struct stat st;
            std::string a = abs + "/" + names[i];
            if (stat(a.c_str(), &st) != 0) continue;
            char b[96];
            snprintf(b, sizeof(b), "\",\"dir\":%s,\"size\":%lld,\"mtime\":%lld}",
                     S_ISDIR(st.st_mode) ? "true" : "false", (long long)st.st_size, (long long)st.st_mtime);
            if (j[j.size() - 1] == '}') j += ",";
            j += "{\"name\":\"" + jsonEsc(names[i]) + b;
        }
        j += "]}";
        sendText(fd, 200, j, "application/json");
        return;
    }

    // --- bajar un archivo
    if (path == "/api/get") {
        FILE *f = fopen(abs.c_str(), "rb");
        if (!f || isDir(abs)) { if (f) fclose(f); sendText(fd, 404, "no existe"); return; }
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        std::string base = rel.substr(rel.rfind('/') == std::string::npos ? 0 : rel.rfind('/') + 1);
        std::string extra = queryParam(query, "dl") == "1"
                                ? "Content-Disposition: attachment; filename=\"" + base + "\"\r\n" : "";
        sendHeader(fd, 200, mimeOf(base), size, extra);
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
            if (!sendAll(fd, buf, n)) break;
        fclose(f);
        return;
    }

    // --- bajar una carpeta (un proyecto) en zip
    if (path == "/api/zip") {
        if (rel.empty() || !isDir(abs)) { sendText(fd, 404, "no existe"); return; }
        std::string base = rel.substr(rel.rfind('/') == std::string::npos ? 0 : rel.rfind('/') + 1);
        sendZip(fd, abs, base);
        return;
    }

    if (method != "POST") { sendText(fd, 405, "usa POST"); return; }
    if (rel.empty()) { sendText(fd, 403, "ahi no"); return; }

    // --- subir un archivo (el cuerpo es el archivo tal cual)
    if (path == "/api/put") {
        size_t slash = abs.rfind('/');
        mkdirs(abs.substr(0, slash));
        std::string tmp = abs + ".subiendo";
        FILE *f = fopen(tmp.c_str(), "wb");
        if (!f) { sendText(fd, 500, "no pude escribir"); return; }
        long long got = 0;
        if (!body.empty()) {
            size_t take = (size_t)std::min<long long>((long long)body.size(), contentLength);
            fwrite(body.data(), 1, take, f);
            got = (long long)take;
        }
        while (got < contentLength) {
            ssize_t n = recv(fd, buf, (size_t)std::min<long long>((long long)sizeof(buf), contentLength - got), 0);
            if (n <= 0) break;
            fwrite(buf, 1, (size_t)n, f);
            got += n;
        }
        fclose(f);
        if (got != contentLength) { unlink(tmp.c_str()); sendText(fd, 400, "se corto la subida"); return; }
        rename(tmp.c_str(), abs.c_str());
        sendText(fd, 200, "{\"ok\":true}", "application/json");
        return;
    }
    if (path == "/api/mkdir") {
        mkdirs(abs);
        sendText(fd, 200, "{\"ok\":true}", "application/json");
        return;
    }
    if (path == "/api/delete") {
        bool ok = removeTree(abs);
        sendText(fd, ok ? 200 : 500, ok ? "{\"ok\":true}" : "{\"ok\":false}", "application/json");
        return;
    }
    if (path == "/api/rename") {
        std::string to;
        if (!cleanRel(queryParam(query, "to"), to) || to.empty()) { sendText(fd, 403, "no"); return; }
        std::string toAbs = root_ + "/" + to;
        mkdirs(toAbs.substr(0, toAbs.rfind('/')));
        bool ok = rename(abs.c_str(), toAbs.c_str()) == 0;
        sendText(fd, ok ? 200 : 500, ok ? "{\"ok\":true}" : "{\"ok\":false}", "application/json");
        return;
    }
    sendText(fd, 404, "no existe");
}

/************************************************************
 los hilos
 ************************************************************/

// una conexión por hilo (el navegador abre varias a la vez)
static volatile int activeConns = 0;
struct ConnArg { int fd; };
static void *connThread(void *a) {
    int fd = ((ConnArg *)a)->fd;
    delete (ConnArg *)a;
    __sync_fetch_and_add(&activeConns, 1);
    WebServer::GetInstance()->handleConn(fd);
    __sync_fetch_and_sub(&activeConns, 1);
    return 0;
}

void WebServer::handleConn(int fd) {
    handle(fd);
    shutdown(fd, SHUT_WR);
    close(fd);
}

static void *serveThread(void *) {
    WebServer::GetInstance()->serveLoop();
    return 0;
}
static void *mdnsThread(void *) {
    WebServer::GetInstance()->mdnsLoop();
    return 0;
}

void WebServer::serveLoop() {
    for (;;) {
        struct sockaddr_in cli;
        socklen_t len = sizeof(cli);
        int fd = accept(listenFd_, (struct sockaddr *)&cli, &len);
        if (fd < 0) {
            if (errno == EINTR) continue;
            usleep(100000);
            continue;
        }
        if (activeConns >= 12) {   // demasiadas: se atiende aquí mismo
            handleConn(fd);
            continue;
        }
        ConnArg *arg = new ConnArg;
        arg->fd = fd;
        pthread_t t;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_attr_setstacksize(&attr, 256 * 1024);
        if (pthread_create(&t, &attr, connThread, arg) != 0) {
            delete arg;
            handleConn(fd);
        }
        pthread_attr_destroy(&attr);
    }
}

void WebServer::Start() {
    if (running_) return;
    Config *config = Config::GetInstance();
    const char *on = config->GetValue("WEBSERVER");
    if (on && (!strcmp(on, "NO") || !strcmp(on, "no"))) return;
    const char *nm = config->GetValue("WEBNAME");
    name_ = nm ? nm : "chancho";
    const char *pv = config->GetValue("WEBPORT");
    int want = pv ? atoi(pv) : 80;

    char real[PATH_MAX];
    std::string r = Path("root:").GetPath();
    root_ = realpath(r.c_str(), real) ? real : r;
    while (root_.size() > 1 && root_[root_.size() - 1] == '/') root_.erase(root_.size() - 1);
    std::string w = Path("bin:web").GetPath();
    web_ = realpath(w.c_str(), real) ? real : w;

    signal(SIGPIPE, SIG_IGN);
    int ports[2] = {want, 8080};
    for (int k = 0; k < 2 && listenFd_ < 0; k++) {
        if (k == 1 && want == 8080) break;
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) continue;
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        fcntl(fd, F_SETFD, FD_CLOEXEC);
        struct sockaddr_in a;
        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_port = htons((unsigned short)ports[k]);
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(fd, (struct sockaddr *)&a, sizeof(a)) == 0 && listen(fd, 8) == 0) {
            listenFd_ = fd;
            port_ = ports[k];
        } else {
            close(fd);
        }
    }
    if (listenFd_ < 0) {
        Trace::Log("WEB", "no pude abrir el servidor");
        return;
    }
    running_ = true;
    Trace::Log("WEB", "http://%s.local:%d  (carpeta %s)", name_.c_str(), port_, root_.c_str());
    pthread_t t;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&t, &attr, serveThread, 0);
    pthread_create(&t, &attr, mdnsThread, 0);
    pthread_attr_destroy(&attr);
}

std::string WebServer::LocalIP() {
    struct ifaddrs *ifs = 0, *i;
    std::string best, any;
    if (getifaddrs(&ifs) != 0) return "";
    for (i = ifs; i; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
        if (!(i->ifa_flags & IFF_UP) || (i->ifa_flags & IFF_LOOPBACK)) continue;
        char b[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &((struct sockaddr_in *)i->ifa_addr)->sin_addr, b, sizeof(b));
        if (!strncmp(i->ifa_name, "wl", 2) && best.empty()) best = b;
        if (any.empty()) any = b;
    }
    freeifaddrs(ifs);
    return best.empty() ? any : best;
}

std::string WebServer::Address() {
    if (!running_) return "";
    std::string ip = LocalIP();
    if (ip.empty()) return "sin wifi";
    char b[96];
    if (port_ == 80) snprintf(b, sizeof(b), "%s.local   %s", name_.c_str(), ip.c_str());
    else snprintf(b, sizeof(b), "%s.local:%d   %s:%d", name_.c_str(), port_, ip.c_str(), port_);
    return b;
}

/************************************************************
 mDNS: contesta "chancho.local" con la IP de la consola
 ************************************************************/

// lee un nombre de la pregunta (con punteros de compresión) en minúsculas
static bool readName(const unsigned char *pkt, int len, int &pos, std::string &out) {
    out.clear();
    int p = pos, jumps = 0;
    bool jumped = false;
    while (p < len) {
        int l = pkt[p];
        if (l == 0) { p++; break; }
        if ((l & 0xC0) == 0xC0) {
            if (p + 1 >= len || ++jumps > 8) return false;
            int to = ((l & 0x3F) << 8) | pkt[p + 1];
            if (!jumped) pos = p + 2;
            jumped = true;
            p = to;
            continue;
        }
        if (p + 1 + l > len) return false;
        if (!out.empty()) out += ".";
        for (int k = 0; k < l; k++) out += (char)tolower(pkt[p + 1 + k]);
        p += 1 + l;
    }
    if (!jumped) pos = p;
    return true;
}

static int buildAnswer(unsigned char *out, const std::string &host, unsigned int ip, unsigned short id) {
    int n = 0;
    out[n++] = (unsigned char)(id >> 8); out[n++] = (unsigned char)id;
    out[n++] = 0x84; out[n++] = 0x00;   // respuesta, autoritativa
    out[n++] = 0; out[n++] = 0;         // preguntas
    out[n++] = 0; out[n++] = 1;         // respuestas
    out[n++] = 0; out[n++] = 0; out[n++] = 0; out[n++] = 0;
    size_t p = 0;
    while (p < host.size()) {
        size_t e = host.find('.', p);
        if (e == std::string::npos) e = host.size();
        out[n++] = (unsigned char)(e - p);
        memcpy(out + n, host.data() + p, e - p);
        n += (int)(e - p);
        p = e + 1;
    }
    out[n++] = 0;
    out[n++] = 0; out[n++] = 1;         // A
    out[n++] = 0x80; out[n++] = 1;      // IN, cache flush
    out[n++] = 0; out[n++] = 0; out[n++] = 0; out[n++] = 120;   // TTL
    out[n++] = 0; out[n++] = 4;
    memcpy(out + n, &ip, 4);
    n += 4;
    return n;
}

void WebServer::mdnsLoop() {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#ifdef SO_REUSEPORT
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
#endif
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(5353);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *)&a, sizeof(a)) != 0) {
        Trace::Log("WEB", "mDNS: el puerto 5353 está ocupado (usa la IP)");
        close(fd);
        return;
    }
    std::string host = name_ + ".local";
    for (size_t i = 0; i < host.size(); i++) host[i] = (char)tolower(host[i]);
    struct sockaddr_in grp;
    memset(&grp, 0, sizeof(grp));
    grp.sin_family = AF_INET;
    grp.sin_port = htons(5353);
    inet_pton(AF_INET, "224.0.0.251", &grp.sin_addr);
    unsigned char ttl = 255;
    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

    std::string joinedIp;
    unsigned char pkt[1500], out[512];
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    int announce = 0;
    for (;;) {
        // la red puede llegar después (o cambiar): se vuelve a unir al grupo
        std::string ip = LocalIP();
        if (!ip.empty() && ip != joinedIp) {
            struct ip_mreq m;
            inet_pton(AF_INET, "224.0.0.251", &m.imr_multiaddr);
            inet_pton(AF_INET, ip.c_str(), &m.imr_interface);
            setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &m, sizeof(m));
            setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &m.imr_interface, sizeof(m.imr_interface));
            joinedIp = ip;
            announce = 3;
        }
        if (announce > 0 && !ip.empty()) {   // se presenta solo, por si alguien ya preguntó
            unsigned int addr;
            inet_pton(AF_INET, ip.c_str(), &addr);
            int n = buildAnswer(out, host, addr, 0);
            sendto(fd, out, n, 0, (struct sockaddr *)&grp, sizeof(grp));
            announce--;
        }
        struct sockaddr_in from;
        socklen_t fl = sizeof(from);
        int len = (int)recvfrom(fd, pkt, sizeof(pkt), 0, (struct sockaddr *)&from, &fl);
        if (len < 12 || ip.empty()) continue;
        if (pkt[2] & 0x80) continue;   // es una respuesta, no una pregunta
        int qd = (pkt[4] << 8) | pkt[5];
        int pos = 12;
        bool asked = false, unicast = false;
        for (int k = 0; k < qd && pos < len; k++) {
            std::string qn;
            if (!readName(pkt, len, pos, qn) || pos + 4 > len) break;
            int qtype = (pkt[pos] << 8) | pkt[pos + 1];
            int qclass = (pkt[pos + 2] << 8) | pkt[pos + 3];
            pos += 4;
            if (qn == host && (qtype == 1 || qtype == 255)) {
                asked = true;
                if (qclass & 0x8000) unicast = true;
            }
        }
        if (!asked) continue;
        unsigned int addr;
        inet_pton(AF_INET, ip.c_str(), &addr);
        bool legacy = ntohs(from.sin_port) != 5353;   // preguntas "a la antigua" (dig, etc.)
        unsigned short id = legacy ? (unsigned short)((pkt[0] << 8) | pkt[1]) : 0;
        int n = buildAnswer(out, host, addr, id);
        if (legacy || unicast) sendto(fd, out, n, 0, (struct sockaddr *)&from, fl);
        if (!legacy) sendto(fd, out, n, 0, (struct sockaddr *)&grp, sizeof(grp));
    }
}

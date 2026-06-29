#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <sstream>
#include <string>
#include <thread>

#include "launcher_common.h"

#pragma comment(lib, "ws2_32.lib")

namespace {

constexpr int kWebRelayPort = 6090;
constexpr unsigned short kInputPort = 7331;

const char* const kWebRelayPage = R"PAGE(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no,viewport-fit=cover">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
<meta name="apple-mobile-web-app-title" content="Bandit Mouse">
<title>Bandit Web Mouse Support</title>
<style>
:root{--line:#243444;--muted:#9eb0bf;--text:#edf4f8;--accent:#70c486;--accent2:#69b7cc;}
*{box-sizing:border-box;-webkit-user-select:none;user-select:none;-webkit-tap-highlight-color:transparent;}
html,body{margin:0;height:100%;overflow:hidden;}
body{font:15px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:radial-gradient(circle at top,#102030,#071018 60%);color:var(--text);display:flex;flex-direction:column;height:100dvh;touch-action:none;}
header{display:flex;align-items:center;justify-content:space-between;padding:12px 16px;border-bottom:1px solid var(--line);}
.brand{display:flex;align-items:center;gap:10px;font-weight:600;}
.logo{width:22px;height:22px;border-radius:6px;background:linear-gradient(135deg,var(--accent),var(--accent2));}
.dot{width:10px;height:10px;border-radius:50%;background:#4a5a68;transition:background .2s;}
.dot.live{background:var(--accent);box-shadow:0 0 10px var(--accent);}
.hdr-right{display:flex;align-items:center;gap:10px;color:var(--muted);font-size:13px;}
.stage{flex:1;display:flex;flex-direction:column;min-height:0;}
#pad{flex:1;margin:14px;border:1px solid var(--line);border-radius:16px;background:linear-gradient(180deg,#0d1822,#0a131c);display:flex;align-items:center;justify-content:center;text-align:center;color:var(--muted);position:relative;overflow:hidden;}
#pad .hint{padding:24px;max-width:430px;}
#pad .hint b{color:var(--text);display:block;font-size:17px;margin-bottom:6px;}
#pad.live{border-color:var(--accent);}
.controls{display:flex;flex-direction:column;gap:8px;padding:0 14px 14px;}
.row{display:flex;gap:8px;}
.btn{flex:1 1 0;min-width:60px;min-height:54px;border:1px solid var(--line);border-radius:12px;background:#0e1a24;color:var(--text);font:inherit;font-weight:600;display:flex;align-items:center;justify-content:center;}
.btn:active,.btn.on{background:#16313f;border-color:var(--accent2);color:#fff;}
.btn.wide{flex:2 1 0;}
.sens{display:flex;align-items:center;gap:10px;color:var(--muted);font-size:13px;padding-top:2px;}
.sens input{flex:1;}
.hidden{display:none!important;}
@media (orientation:landscape) and (max-height:560px){
.stage{flex-direction:row;}
#pad{margin:10px 8px 10px 12px;}
.controls{width:240px;justify-content:center;padding:10px 12px 10px 8px;}
.btn{min-height:44px;}
header{padding:8px 16px;}
}
</style>
</head>
<body>
<header>
  <div class="brand"><span class="logo"></span>Bandit Web Mouse Support</div>
  <div class="hdr-right"><span id="statetext">tap pad to start</span><span class="dot" id="dot"></span></div>
</header>
<div class="stage">
<div id="pad"><div class="hint"><b>Touchpad</b>Drag to move</div></div>
<div class="controls">
  <div class="row">
    <button class="btn wide" id="bl">Left</button>
    <button class="btn" id="bm">Middle</button>
    <button class="btn wide" id="br">Right</button>
  </div>
  <div class="row">
    <button class="btn" id="su">Scroll +</button>
    <button class="btn" id="sd">Scroll -</button>
    <button class="btn hidden" id="fs">Fullscreen</button>
  </div>
  <div class="sens"><span>Speed</span><input type="range" id="sens" min="0.4" max="3" step="0.1" value="1.2"><span id="sensv">1.2x</span></div>
</div>
</div>
<script>
(function(){
  let dx=0,dy=0,scroll=0,l=0,r=0,m=0,sens=1.2,dirty=false,captured=false,live=false;
  const pad=document.getElementById('pad'),dot=document.getElementById('dot'),statetext=document.getElementById('statetext');
  const sensEl=document.getElementById('sens'),sensv=document.getElementById('sensv');
  sensEl.addEventListener('input',function(){sens=parseFloat(sensEl.value);sensv.textContent=sens.toFixed(1)+'x';});
  function setLive(v){if(v!==live){live=v;dot.classList.toggle('live',v);pad.classList.toggle('live',v);}}
  function setBtn(w,v){if(w===0)l=v;else if(w===1)m=v;else if(w===2)r=v;dirty=true;}
  pad.addEventListener('click',function(){if('requestPointerLock' in pad)pad.requestPointerLock();});
  document.addEventListener('pointerlockchange',function(){captured=(document.pointerLockElement===pad);statetext.textContent=captured?'mouse captured':'tap pad to start';});
  let lastPt=null;
  pad.addEventListener('mousemove',function(e){if(captured){dx+=e.movementX*sens;dy+=e.movementY*sens;}else if(lastPt){dx+=(e.clientX-lastPt.x)*sens;dy+=(e.clientY-lastPt.y)*sens;}lastPt={x:e.clientX,y:e.clientY};});
  pad.addEventListener('mouseleave',function(){lastPt=null;});
  pad.addEventListener('mousedown',function(e){setBtn(e.button,1);e.preventDefault();});
  window.addEventListener('mouseup',function(e){setBtn(e.button,0);});
  pad.addEventListener('contextmenu',function(e){e.preventDefault();});
  pad.addEventListener('wheel',function(e){scroll+=(e.deltaY<0?1:-1);dirty=true;e.preventDefault();},{passive:false});
  let last=null;
  pad.addEventListener('touchstart',function(e){last=e.changedTouches[0];e.preventDefault();},{passive:false});
  pad.addEventListener('touchmove',function(e){var t=e.changedTouches[0];if(last){dx+=(t.clientX-last.clientX)*sens*1.6;dy+=(t.clientY-last.clientY)*sens*1.6;}last=t;e.preventDefault();},{passive:false});
  pad.addEventListener('touchend',function(e){last=null;e.preventDefault();},{passive:false});
  function holdBtn(id,w){var el=document.getElementById(id);
    var dn=function(e){setBtn(w,1);el.classList.add('on');e.preventDefault();};
    var up=function(e){setBtn(w,0);el.classList.remove('on');e.preventDefault();};
    el.addEventListener('mousedown',dn);el.addEventListener('mouseup',up);el.addEventListener('mouseleave',up);
    el.addEventListener('touchstart',dn,{passive:false});el.addEventListener('touchend',up,{passive:false});}
  holdBtn('bl',0);holdBtn('bm',1);holdBtn('br',2);
  function scrollBtn(id,a){var el=document.getElementById(id);
    var go=function(e){scroll+=a;dirty=true;e.preventDefault();};
    el.addEventListener('mousedown',go);el.addEventListener('touchstart',go,{passive:false});}
  scrollBtn('su',1);scrollBtn('sd',-1);
  var fsEl=document.documentElement,fsReq=fsEl.requestFullscreen||fsEl.webkitRequestFullscreen,fsBtn=document.getElementById('fs');
  if(fsReq){fsBtn.classList.remove('hidden');fsBtn.addEventListener('click',function(){var d=document,isFs=d.fullscreenElement||d.webkitFullscreenElement;if(isFs){(d.exitFullscreen||d.webkitExitFullscreen).call(d);}else{fsReq.call(fsEl);}});}
  var lastSend=0,lastPing=0,fails=0;
  function send(b){fetch('/input',{method:'POST',body:b,keepalive:true}).then(function(){fails=0;setLive(true);}).catch(function(){if(++fails>20)setLive(false);});}
  function loop(){
    var now=performance.now();
    var sx=Math.round(dx),sy=Math.round(dy);
    if(now-lastSend>=8&&(sx||sy||scroll||dirty)){
      send(sx+','+sy+','+l+','+r+','+m+','+scroll+',-1,-1');
      dx-=sx;dy-=sy;scroll=0;dirty=false;lastSend=now;lastPing=now;
    }else if(now-lastPing>=700){
      send('0,0,'+l+','+r+','+m+',0,-1,-1');lastPing=now;
    }
    requestAnimationFrame(loop);
  }
  requestAnimationFrame(loop);
})();
</script>
</body>
</html>
)PAGE";

bool SendAll(SOCKET s, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        const int chunk = send(s, data + sent, (int)((std::min)(len - sent, (size_t)(64 * 1024))), 0);
        if (chunk <= 0) return false;
        sent += (size_t)chunk;
    }
    return true;
}

bool SendResponse(SOCKET s, int status, const char* statusText, const char* contentType, const std::string& body) {
    std::ostringstream head;
    head << "HTTP/1.1 " << status << " " << statusText << "\r\n"
         << "Content-Type: " << contentType << "\r\n"
         << "Content-Length: " << body.size() << "\r\n"
         << "Cache-Control: no-store\r\n"
         << "Access-Control-Allow-Origin: *\r\n"
         << "Connection: keep-alive\r\n\r\n";
    const std::string h = head.str();
    if (!SendAll(s, h.data(), h.size())) return false;
    if (!body.empty() && !SendAll(s, body.data(), body.size())) return false;
    return true;
}

bool ReadRequest(SOCKET s, std::string& method, std::string& path, std::string& body) {
    std::string data;
    char buffer[4096];
    size_t headerEnd = std::string::npos;
    while (data.size() < 256 * 1024) {
        const int read = recv(s, buffer, sizeof(buffer), 0);
        if (read <= 0) return false;
        data.append(buffer, (size_t)read);
        headerEnd = data.find("\r\n\r\n");
        if (headerEnd != std::string::npos) break;
    }
    if (headerEnd == std::string::npos) return false;

    const std::string headPart = data.substr(0, headerEnd);
    const size_t firstLineEnd = headPart.find("\r\n");
    const std::string firstLine = headPart.substr(0, firstLineEnd == std::string::npos ? headPart.size() : firstLineEnd);
    std::istringstream first(firstLine);
    std::string target, version;
    first >> method >> target >> version;
    const size_t q = target.find('?');
    path = q == std::string::npos ? target : target.substr(0, q);

    size_t contentLength = 0;
    std::string lower = headPart;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)tolower(c); });
    const size_t cl = lower.find("content-length:");
    if (cl != std::string::npos) {
        contentLength = (size_t)strtoul(headPart.c_str() + cl + 15, nullptr, 10);
    }
    if (contentLength > 4096) contentLength = 4096;

    body = data.substr(headerEnd + 4);
    while (body.size() < contentLength) {
        const int read = recv(s, buffer, sizeof(buffer), 0);
        if (read <= 0) return false;
        body.append(buffer, (size_t)read);
    }
    if (body.size() > contentLength) body.resize(contentLength);
    return true;
}

bool ValidPacket(const std::string& body) {
    if (body.empty() || body.size() > 96) return false;
    int commas = 0;
    for (char c : body) {
        if (c >= '0' && c <= '9') continue;
        if (c == ',') { ++commas; continue; }
        if (c == '-' || c == '.') continue;
        return false;
    }
    return commas == 7;
}

class WebRelayServer {
public:
    void Start() {
        if (running_.load()) return;
        if (thread_.joinable()) thread_.join();
        stop_.store(false);
        running_.store(true);
        thread_ = std::thread([this]() { ThreadMain(); });
    }

    void Stop() {
        if (!running_.load()) {
            if (thread_.joinable()) thread_.join();
            return;
        }
        stop_.store(true);
        SOCKET ls = listenSocket_.exchange(INVALID_SOCKET);
        if (ls != INVALID_SOCKET) {
            shutdown(ls, SD_BOTH);
            closesocket(ls);
        }
        if (thread_.joinable()) thread_.join();
        running_.store(false);
    }

    bool Running() const { return running_.load(); }

private:
    std::atomic<bool> running_{ false };
    std::atomic<bool> stop_{ false };
    std::atomic<SOCKET> listenSocket_{ INVALID_SOCKET };
    std::thread thread_;

    void ThreadMain() {
        WSADATA wsa = {};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            running_.store(false);
            return;
        }
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) {
            WSACleanup();
            running_.store(false);
            return;
        }
        BOOL reuse = TRUE;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons((unsigned short)kWebRelayPort);
        if (bind(s, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(s, 8) != 0) {
            WriteLogF(L"Web relay bind/listen failed port=%d err=%d", kWebRelayPort, WSAGetLastError());
            closesocket(s);
            WSACleanup();
            running_.store(false);
            return;
        }
        listenSocket_.store(s);
        WriteLogF(L"Web mouse relay listening on port %d", kWebRelayPort);

        while (!stop_.load()) {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(s, &readSet);
            timeval tv = {};
            tv.tv_sec = 0;
            tv.tv_usec = 250000;
            if (select(0, &readSet, nullptr, nullptr, &tv) <= 0) continue;
            SOCKET client = accept(s, nullptr, nullptr);
            if (client == INVALID_SOCKET) continue;
            if (stop_.load()) {
                closesocket(client);
                break;
            }
            std::thread([this, client]() {
                HandleClient(client);
                closesocket(client);
            }).detach();
        }

        SOCKET old = listenSocket_.exchange(INVALID_SOCKET);
        if (old != INVALID_SOCKET) closesocket(old);
        WSACleanup();
        WriteLog(L"Web mouse relay stopped");
    }

    void HandleClient(SOCKET client) {
        SOCKET udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        sockaddr_in dst = {};
        dst.sin_family = AF_INET;
        dst.sin_port = htons(kInputPort);
        inet_pton(AF_INET, "127.0.0.1", &dst.sin_addr);

        while (!stop_.load()) {
            std::string method, path, body;
            if (!ReadRequest(client, method, path, body)) break;

            if (method == "POST" && path == "/input") {
                if (udp != INVALID_SOCKET && ValidPacket(body)) {
                    sendto(udp, body.data(), (int)body.size(), 0, (const sockaddr*)&dst, sizeof(dst));
                }
                if (!SendResponse(client, 204, "No Content", "text/plain", std::string())) break;
            } else if (method == "GET" && (path == "/" || path == "/index.html")) {
                if (!SendResponse(client, 200, "OK", "text/html; charset=utf-8", kWebRelayPage)) break;
            } else if (method == "GET" && path == "/health") {
                if (!SendResponse(client, 200, "OK", "text/plain", "ok")) break;
            } else {
                if (!SendResponse(client, 404, "Not Found", "text/plain", "Not found")) break;
            }
        }

        if (udp != INVALID_SOCKET) closesocket(udp);
    }
};

WebRelayServer g_webRelayServer;

}

void StartWebRelayServer() { g_webRelayServer.Start(); }
void StopWebRelayServer() { g_webRelayServer.Stop(); }
bool WebRelayServerRunning() { return g_webRelayServer.Running(); }

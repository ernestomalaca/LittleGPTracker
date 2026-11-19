#ifndef _WEB_SERVER_H_
#define _WEB_SERVER_H_

// trk2als: chancho.local. Un servidorcito HTTP en su propio hilo (para
// manejar los proyectos y los samples desde la compu o el celular) y un
// responder de mDNS para que el nombre funcione sin instalar nada.
//
// config.xml:
//   <WEBSERVER value="YES"/>     (NO lo apaga)
//   <WEBPORT value="80"/>        (si no se puede, prueba 8080)
//   <WEBNAME value="chancho"/>   (chancho.local)

#include <string>

class WebServer {
public:
    static WebServer *GetInstance();
    void Start();
    bool IsRunning() { return running_; }
    int Port() { return port_; }
    const std::string &Name() { return name_; }
    // la IP de la red local (vacía si no hay)
    std::string LocalIP();
    // lo que se ve en la pantalla: "chancho.local  192.168.1.20" o un aviso
    std::string Address();

    // (para los hilos)
    void serveLoop();
    void mdnsLoop();
    void handleConn(int fd);

private:
    WebServer();
    void handle(int fd);
    bool running_;
    int listenFd_;
    int port_;
    std::string name_;
    std::string root_;   // la carpeta de los proyectos (root:)
    std::string web_;    // la carpeta de la página (bin:web)
};

#endif

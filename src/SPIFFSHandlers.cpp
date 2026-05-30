#include "SPIFFSHandlers.h"

#include <ArduinoJson.h>
#include <SPIFFS.h>

namespace SpiffsHandlers
{
    void HandleList(WebServer &server)
    {
        StaticJsonDocument<1024> doc;
        JsonArray files = doc.createNestedArray("files");

        File root = SPIFFS.open("/");
        if (!root)
        {
            server.send(500, "application/json", "{\"status\":\"cannot open root\"}");
            return;
        }

        File file = root.openNextFile();
        while (file)
        {
            JsonObject f = files.createNestedObject();
            f["name"] = String(file.name());
            f["size"] = file.size();
            file = root.openNextFile();
        }
        root.close();

        String resp;
        serializeJson(doc, resp);
        server.send(200, "application/json", resp);
    }

    void HandleDownload(WebServer &server)
    {
        String path = server.arg("path");
        if (path.length() == 0)
        {
            server.send(400, "application/json", "{\"status\":\"missing path\"}");
            return;
        }
        if (!path.startsWith("/"))
            path = "/" + path;

        if (!SPIFFS.exists(path))
        {
            server.send(404, "application/json", "{\"status\":\"file not found\"}");
            return;
        }

        File file = SPIFFS.open(path, "r");
        if (!file)
        {
            server.send(500, "application/json", "{\"status\":\"unable to open file\"}");
            return;
        }

        String filename = path;
        filename.remove(0, filename.lastIndexOf('/') + 1);
        server.sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
        server.streamFile(file, "application/octet-stream");
        file.close();
    }

    void HandleUpload(WebServer &server)
    {
        String path = server.arg("path");
        if (path.length() == 0)
        {
            server.send(400, "application/json", "{\"status\":\"missing path\"}");
            return;
        }
        if (!path.startsWith("/"))
            path = "/" + path;

        if (!server.hasArg("plain"))
        {
            server.send(400, "application/json", "{\"status\":\"missing body\"}");
            return;
        }

        String body = server.arg("plain");
        File file = SPIFFS.open(path, "w");
        if (!file)
        {
            server.send(500, "application/json", "{\"status\":\"cannot open file for writing\"}");
            return;
        }
        file.write((const uint8_t *)body.c_str(), body.length());
        file.close();

        server.send(200, "application/json", "{\"status\":\"ok\"}");
    }

    void HandleDelete(WebServer &server)
    {
        String path = server.arg("path");
        if (path.length() == 0)
        {
            server.send(400, "application/json", "{\"status\":\"missing path\"}");
            return;
        }
        if (!path.startsWith("/"))
            path = "/" + path;

        if (!SPIFFS.exists(path))
        {
            server.send(404, "application/json", "{\"status\":\"file not found\"}");
            return;
        }

        if (SPIFFS.remove(path))
        {
            server.send(200, "application/json", "{\"status\":\"deleted\"}");
        }
        else
        {
            server.send(500, "application/json", "{\"status\":\"delete failed\"}");
        }
    }
}

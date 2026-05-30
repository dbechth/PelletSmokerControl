#pragma once

#include <WebServer.h>

namespace SpiffsHandlers
{
    void HandleList(WebServer &server);
    void HandleDownload(WebServer &server);
    void HandleUpload(WebServer &server);
    void HandleDelete(WebServer &server);
}

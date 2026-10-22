#include "Viewport.h"
#include "ViewportClient.h"
namespace Engine {
    Viewport::Viewport(ViewportClient* inClient) {
        _client = inClient;
    }



}
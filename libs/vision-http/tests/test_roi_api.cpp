#include <cassert>

#include "catcheye/http/http_server.hpp"
#include "catcheye/http/roi_api.hpp"

int main()
{
    assert(catcheye::http::json_error_body("bad") == "{\"error\":\"bad\"}");
    assert(catcheye::http::escape_json_string("\"x\"") == "\\\"x\\\"");
    return 0;
}

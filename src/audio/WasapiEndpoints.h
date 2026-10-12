#ifndef _WASAPIENDPOINTS_H_
#define _WASAPIENDPOINTS_H_

#include "../util/WinSupport.h"

#include <mmdeviceapi.h>

#include <string>
#include <vector>

// The audio endpoints Windows offers (render or capture), by the ids
// DeviceSettings names them with.
namespace wasapi {

struct Endpoint {
  std::string id;
  std::string name;
};

// Active endpoints, in the system's order.
std::vector<Endpoint> listEndpoints(bool capture);

// The endpoint a DeviceSettings audio name selects: the default one for
// ""/"default", else the "wasapi:<id>" endpoint if it is active. Empty if
// there is none (the cause is in `error`).
winsupport::ComPtr<IMMDevice> openEndpoint(const std::string & name, bool capture, std::string & error);

} // namespace wasapi

#endif

#include "devices/gopro/protocol.h"

namespace gopro {

Packet buildSetShutter(bool enabled) {
  Packet packet;
  packet.bytes[0] = 0x03;
  packet.bytes[1] = kSetShutterCommand;
  packet.bytes[2] = 0x01;
  packet.bytes[3] = enabled ? 0x01 : 0x00;
  packet.len = 4;
  return packet;
}

Packet buildSetPairingState() {
  Packet packet;
  packet.bytes[0] = 0x03;
  packet.bytes[1] = kSetPairingStateCommand;
  packet.bytes[2] = 0x01;
  packet.bytes[3] = 0x01;
  packet.len = 4;
  return packet;
}

Response parseResponse(const uint8_t* data, size_t len) {
  Response response;
  if (data == nullptr || len < 3 || data[0] < 2 || data[0] + 1 > len) {
    return response;
  }
  response.valid = true;
  response.command = data[1];
  response.status = data[2];
  return response;
}

}  // namespace gopro

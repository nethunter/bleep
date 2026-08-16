#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
struct Button { int y; const char* label; const char* color; };

bool render(const char* name, const char* title, const char* detail,
            int progress, const std::vector<Button>& buttons) {
  std::filesystem::create_directories("sim/screenshots");
  std::ofstream output(std::string("sim/screenshots/") + name + ".svg");
  if (!output) return false;
  output << "<svg xmlns='http://www.w3.org/2000/svg' width='240' height='240' viewBox='0 0 240 240'>"
            "<defs><clipPath id='round'><circle cx='120' cy='120' r='120'/></clipPath></defs>"
            "<g clip-path='url(#round)'><rect width='240' height='240' fill='#000'/>"
            "<g fill='#fff' font-family='Montserrat,Arial,sans-serif' text-anchor='middle'>"
            "<text x='120' y='42' font-size='12'>BLE(E)P RECOVERY</text>"
            "<text x='120' y='77' font-size='16' fill='#00d8df'>" << title << "</text>"
            "<text x='120' y='104' font-size='12'>" << detail << "</text>";
  if (progress >= 0) {
    output << "<rect x='42' y='120' width='156' height='12' rx='4' fill='none' stroke='#777'/>"
              "<rect x='44' y='122' width='" << (progress * 152 / 100)
           << "' height='8' rx='3' fill='#00d8df'/>";
  }
  for (const Button& button : buttons) {
    output << "<rect x='42' y='" << button.y << "' width='156' height='30' rx='8' fill='"
           << button.color << "'/><text x='120' y='" << (button.y + 20)
           << "' font-size='12' font-weight='600'>" << button.label << "</text>";
  }
  output << "</g></g></svg>";
  return true;
}
}  // namespace

int main() {
  bool ok = true;
  ok &= render("recovery_ready", "READY", "", -1,
               {{110, "BOOT FIRMWARE", "#006b32"}, {140, "INSTALL STABLE", "#007078"},
                {170, "FACTORY RESET", "#780000"}});
  ok &= render("recovery_installing", "INSTALLING", "Firmware update", 55, {});
  ok &= render("recovery_factory_reset", "RESETTING", "Erasing saved data", 100, {});
  ok &= render("recovery_no_wifi", "RECOVERY FAILED", "Wi-Fi unavailable", -1,
               {{110, "BOOT FIRMWARE", "#006b32"}, {140, "INSTALL STABLE", "#007078"},
                {170, "FACTORY RESET", "#780000"}});
  return ok ? 0 : 1;
}

#include "../../main/boards/lckfb/szpi-esp32s3/livis_format.h"
#include <cassert>
int main() {
    assert(livis::FormatTemperature(10.4) == "10°");
    assert(livis::FormatTemperature(-2.6) == "-3°");
    assert(livis::FormatTemperatureRange("12°", "-2°") == "-2° ~ 12°");
    assert(livis::FormatPrice("48.640") == "48.64");
    assert(livis::FormatPrice("12.1950") == "12.20");
    assert(livis::FormatPrice("0") == "--");
    assert(livis::FormatPrice("nan") == "--");
    assert(livis::FormatPrice("123broken") == "--");
    assert(livis::FormatChange("-3.357%") == "-3.36%");
    assert(livis::FormatChange("2.3%") == "+2.30%");
    assert(livis::FormatChange("-0.00%") == "+0.00%");
    assert(livis::FormatChange("--") == "--");
    assert(livis::FormatChange("%") == "--");
    assert(livis::FormatChange("1.2%junk") == "--");
}

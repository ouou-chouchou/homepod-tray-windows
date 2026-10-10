// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <string>
#include <vector>
namespace homepod {
struct Receiver { std::string name, ip; unsigned short port=7000; };
std::vector<Receiver> discoverReceivers(); // Explicit action only; never scans a subnet.
}

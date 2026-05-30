#pragma once

struct SmokerConfig;

bool SaveConfigToSPIFFS(const SmokerConfig &config);
bool LoadConfigFromSPIFFS(SmokerConfig &config);

#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <vector>
#include <map>
#include <chrono>
#include <thread>
#include <ctime>
#include <iomanip>

namespace fs = std::filesystem;

// Data Structures

struct NetworkStatsSnapshot {
    unsigned long long ReceivedBytes = 0;
    unsigned long long TransmittedBytes = 0;
};

struct InterfaceStateTracker {
    // this will include the one time fetched data from the file

    bool IsFirstMeasurement = true;
    unsigned long long LastMeasuredReceivedBytes = 0;
    unsigned long long LastMeasuredTransmittedBytes = 0;
    std::map<std::string, NetworkStatsSnapshot> DailyUsageHistory;
};

enum class InterfaceCategory { Physical, Virtual, All };

// --- Helpers ---

std::string GetCurrentDateAsString() {
    auto now = std::time(nullptr);
    auto localTime = *std::localtime(&now);
    std::ostringstream dateStream;
    dateStream << std::put_time(&localTime, "%Y-%m-%d");
    return dateStream.str();
}

unsigned long long ConvertBytesToMegabytes(unsigned long long totalBytes) {
    return totalBytes / 1048576;
}
unsigned long long ConvertBytesToGigabytes(unsigned long long totalBytes){
    return totalBytes / 1073741824;
}

// --- System Info ---

std::vector<std::string> GetAvailableNetworkInterfaces(InterfaceCategory category) {
    std::vector<std::string> interfaceNames;
    std::error_code errorCode;
    for (const auto& entry : fs::directory_iterator("/sys/class/net", errorCode)) {
        bool isPhysical = fs::exists(entry.path() / "device", errorCode);
        if (category == InterfaceCategory::All ||
           (category == InterfaceCategory::Physical && isPhysical) ||
           (category == InterfaceCategory::Virtual && !isPhysical)) {
            interfaceNames.push_back(entry.path().filename().string());
        }
    }
    return interfaceNames;
}

std::map<std::string, NetworkStatsSnapshot> FetchCurrentKernelNetworkStats() {
    std::map<std::string, NetworkStatsSnapshot> currentStats;
    std::ifstream netDevFile("/proc/net/dev");
    std::string line;
    std::getline(netDevFile, line); std::getline(netDevFile, line); // Skip headers

    while (std::getline(netDevFile, line)) {
        std::replace(line.begin(), line.end(), ':', ' ');
        std::stringstream lineScanner(line);
        std::string name;
        NetworkStatsSnapshot stats;
        lineScanner >> name >> stats.ReceivedBytes;
        for (int i = 0; i < 7; i++) { std::string dummy; lineScanner >> dummy; }
        lineScanner >> stats.TransmittedBytes;
        currentStats[name] = stats;
    }
    return currentStats;
}

// --- File Management ---

// ~/NetworkUsage/PhysicalDevices/wlo1

void SaveDailyStatsToDisk(const fs::path& filePath, const std::map<std::string, NetworkStatsSnapshot>& history) {
    std::ofstream outputFile(filePath, std::ios::trunc);
    if (!outputFile.is_open()) return;

    for (const auto& [date, stats] : history) {
        unsigned long long receivedMB = ConvertBytesToMegabytes(stats.ReceivedBytes);
        unsigned long long transmittedMB = ConvertBytesToMegabytes(stats.TransmittedBytes);
        unsigned long long totalMB = receivedMB + transmittedMB;

        outputFile << date << ": Transmitted: " << transmittedMB << "MB , "
                   << "Received: " << receivedMB << "MB, "
                   << "Total: " << totalMB << "MB\n";
    }
}

void LoadHistoryFromDisk(const fs::path& filePath, std::map<std::string, NetworkStatsSnapshot>& usageHistory) {
    // If the file doesn't even exist yet, there is nothing to load
    if (fs::exists(filePath) == false) {
        return;
    }

    std::ifstream inputFile(filePath);
    std::string currentLineFromFile;

    const std::string transmittedMarker = "Transmitted: ";
    const std::string receivedMarker = "Received: ";

    while (std::getline(inputFile, currentLineFromFile)) {
        try {
            // find the date
            size_t colonPosition = currentLineFromFile.find(':');
            if (colonPosition == std::string::npos) continue;

            std::string dateKey = currentLineFromFile.substr(0, colonPosition);

            // Step 2: Find the Transmitted number
            size_t transmittedStart = currentLineFromFile.find(transmittedMarker) + transmittedMarker.length();
            size_t transmittedEnd = currentLineFromFile.find("MB", transmittedStart);

            // Step 3: Find the Received number
            size_t receivedStart = currentLineFromFile.find(receivedMarker) + receivedMarker.length();
            size_t receivedEnd = currentLineFromFile.find("MB", receivedStart);

            // If we found both numbers, convert the text back to real numbers
            if (transmittedStart != std::string::npos && receivedStart != std::string::npos) {

                std::string transmittedText = currentLineFromFile.substr(transmittedStart, transmittedEnd - transmittedStart);
                std::string receivedText = currentLineFromFile.substr(receivedStart, receivedEnd - receivedStart);

                unsigned long long transmittedMegabytes = std::stoull(transmittedText);
                unsigned long long receivedMegabytes = std::stoull(receivedText);

                // Convert Megabytes back into Bytes so our live monitoring math stays accurate
                usageHistory[dateKey].TransmittedBytes = transmittedMegabytes * 1048576;
                usageHistory[dateKey].ReceivedBytes = receivedMegabytes * 1048576;
            }
        }
        catch (...) {
            // If a line is corrupted or someone edited the file manually and broke it, skip it
            continue;
        }
    }
}


// --- Main Engine ---
void RunNetworkMonitoringLoop(fs::path rootStoragePath) {
    // Define the paths for our two categories
    fs::path physicalInterfacesFolder = rootStoragePath / "PhysicalInterfaces";
    fs::path virtualInterfacesFolder = rootStoragePath / "VirtualInterfaces";

    // Ensure the folders exist on the hard drive
    fs::create_directories(physicalInterfacesFolder);
    fs::create_directories(virtualInterfacesFolder);

    // This map stores the state of every interface we find
    std::map<std::string, InterfaceStateTracker> internalStateCollection;

    // These track the "Total" files for each category
    InterfaceStateTracker physicalAggregateTracker;
    InterfaceStateTracker virtualAggregateTracker;

    // Before we start, load all existing history so we don't restart from zero
    auto physicalNames = GetAvailableNetworkInterfaces(InterfaceCategory::Physical);
    auto virtualNames = GetAvailableNetworkInterfaces(InterfaceCategory::Virtual);

    for (const auto& name : physicalNames) {
        LoadHistoryFromDisk(physicalInterfacesFolder / name, internalStateCollection[name].DailyUsageHistory);
    }
    for (const auto& name : virtualNames) {
        LoadHistoryFromDisk(virtualInterfacesFolder / name, internalStateCollection[name].DailyUsageHistory);
    }

    // Load the historical totals
    LoadHistoryFromDisk(physicalInterfacesFolder / "TotalPhysicalUsage", physicalAggregateTracker.DailyUsageHistory);
    LoadHistoryFromDisk(virtualInterfacesFolder / "TotalVirtualUsage", virtualAggregateTracker.DailyUsageHistory);

    // The Forever Loop
    while (true) {
        std::string todayDateString = GetCurrentDateAsString();
        auto currentKernelStats = FetchCurrentKernelNetworkStats();

        unsigned long long totalPhysicalChangeReceived = 0;
        unsigned long long totalPhysicalChangeTransmitted = 0;
        unsigned long long totalVirtualChangeReceived = 0;
        unsigned long long totalVirtualChangeTransmitted = 0;

        for (auto& [interfaceName, liveStats] : currentKernelStats) {
            auto& tracker = internalStateCollection[interfaceName];

            // If this is the first time we've seen this interface since the program started,
            // we just set the baseline and wait for the next loop to calculate the difference.
            if (tracker.IsFirstMeasurement == true) {
                tracker.LastMeasuredReceivedBytes = liveStats.ReceivedBytes;
                tracker.LastMeasuredTransmittedBytes = liveStats.TransmittedBytes;
                tracker.IsFirstMeasurement = false;
                continue;
            }

            unsigned long long changeInReceived = 0;
            unsigned long long changeInTransmitted = 0;

            // Check for counter resets (like if the computer rebooted)
            if (liveStats.ReceivedBytes < tracker.LastMeasuredReceivedBytes) {
                changeInReceived = liveStats.ReceivedBytes;
            } else {
                changeInReceived = liveStats.ReceivedBytes - tracker.LastMeasuredReceivedBytes;
            }

            if (liveStats.TransmittedBytes < tracker.LastMeasuredTransmittedBytes) {
                changeInTransmitted = liveStats.TransmittedBytes;
            } else {
                changeInTransmitted = liveStats.TransmittedBytes - tracker.LastMeasuredTransmittedBytes;
            }

            // If any data was used, update the records
            if (changeInReceived > 0 || changeInTransmitted > 0) {
                tracker.DailyUsageHistory[todayDateString].ReceivedBytes += changeInReceived;
                tracker.DailyUsageHistory[todayDateString].TransmittedBytes += changeInTransmitted;

                // Figure out which folder this interface belongs in
                bool isPhysicalInterface = (std::find(physicalNames.begin(), physicalNames.end(), interfaceName) != physicalNames.end());
                fs::path targetSubFolder = isPhysicalInterface ? physicalInterfacesFolder : virtualInterfacesFolder;

                // Save the individual interface file
                SaveDailyStatsToDisk(targetSubFolder / interfaceName, tracker.DailyUsageHistory);

                // Add this small change to our running total for the aggregates
                if (isPhysicalInterface) {
                    totalPhysicalChangeReceived += changeInReceived;
                    totalPhysicalChangeTransmitted += changeInTransmitted;
                } else {
                    totalVirtualChangeReceived += changeInReceived;
                    totalVirtualChangeTransmitted += changeInTransmitted;
                }
            }

            // Update the baseline for the next 3-second check
            tracker.LastMeasuredReceivedBytes = liveStats.ReceivedBytes;
            tracker.LastMeasuredTransmittedBytes = liveStats.TransmittedBytes;
        }

        // Final Step: Update the "Total" aggregate files if any changes happened
        if (totalPhysicalChangeReceived > 0 || totalPhysicalChangeTransmitted > 0) {
            physicalAggregateTracker.DailyUsageHistory[todayDateString].ReceivedBytes += totalPhysicalChangeReceived;
            physicalAggregateTracker.DailyUsageHistory[todayDateString].TransmittedBytes += totalPhysicalChangeTransmitted;
            SaveDailyStatsToDisk(physicalInterfacesFolder / "TotalPhysicalUsage", physicalAggregateTracker.DailyUsageHistory);
        }

        if (totalVirtualChangeReceived > 0 || totalVirtualChangeTransmitted > 0) {
            virtualAggregateTracker.DailyUsageHistory[todayDateString].ReceivedBytes += totalVirtualChangeReceived;
            virtualAggregateTracker.DailyUsageHistory[todayDateString].TransmittedBytes += totalVirtualChangeTransmitted;
            SaveDailyStatsToDisk(virtualInterfacesFolder / "TotalVirtualUsage", virtualAggregateTracker.DailyUsageHistory);
        }

        // Wait for 3 seconds before checking the kernel again
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
}

int main() {
    const char* homeDir = std::getenv("HOME");
    if (!homeDir) return 1;

    fs::path rootPath = fs::path(homeDir) / "NetworkUsage";
    RunNetworkMonitoringLoop(rootPath);

    return 0;
}

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <unordered_map>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

class SystemMonitor {
public:
    SystemMonitor(const std::string& configPath) {
        loadConfig(configPath);
    }

    void start() {
        running_ = true;
        monitorThread_ = std::thread(&SystemMonitor::monitorLoop, this);
    }

    void stop() {
        running_ = false;
        if (monitorThread_.joinable()) {
            monitorThread_.join();
        }
    }

private:
    struct MetricConfig {
        std::string type;
        std::vector<int> ids;
        std::vector<std::string> specs;
    };

    struct OutputConfig {
        std::string type;
        std::string path;
    };

    int period_;
    std::vector<MetricConfig> metrics_;
    std::vector<OutputConfig> outputs_;
    std::thread monitorThread_;
    bool running_ = false;

    void loadConfig(const std::string& path) {
        try {
            std::ifstream configFile(path);
            if (!configFile.is_open()) {
                throw std::runtime_error("Не получается открыть файл конфига: " + path);
            }

            json config;
            configFile >> config;

            // Загружаем период опроса
            period_ = config["settings"]["period"].get<int>();

            // Загружаем конфигурацию метрик
            for (const auto& metric : config["metrics"]) {
                MetricConfig metricConfig;
                metricConfig.type = metric["type"].get<std::string>();
                
                if (metric.contains("ids")) {
                    for (const auto& id : metric["ids"]) {
                        metricConfig.ids.push_back(id.get<int>());
                    }
                }
                
                if (metric.contains("spec")) {
                    for (const auto& spec : metric["spec"]) {
                        metricConfig.specs.push_back(spec.get<std::string>());
                    }
                }
                
                metrics_.push_back(metricConfig);
            }

            // Загружаем конфигурацию вывода
            for (const auto& output : config["outputs"]) {
                OutputConfig outputConfig;
                outputConfig.type = output["type"].get<std::string>();
                
                if (output.contains("path")) {
                    outputConfig.path = output["path"].get<std::string>();
                }

                outputs_.push_back(outputConfig);
            }
        } catch (const std::exception& e) {
            std::cerr << "Error loading config: " << e.what() << std::endl;
            throw;
        }
    }

    //Основной метод мониторинга
    void monitorLoop() {
        while (running_) {
            auto metricsData = collectMetrics();
            outputMetrics(metricsData);
            std::this_thread::sleep_for(std::chrono::seconds(period_));
        }
    }

    //Сбор метрик
    json collectMetrics() {
        json metricsData;
        metricsData["timestamp"] = getCurrentTimestamp();

        for (const auto& metric : metrics_) {
            if (metric.type == "cpu") {
                metricsData["cpu"] = getCpuMetrics(metric.ids);
            } else if (metric.type == "memory") {
                metricsData["memory"] = getMemoryMetrics(metric.specs);
            }
        }

        return metricsData;
    }

    //Получение метрик процессора
    json getCpuMetrics(const std::vector<int>& coreIds) {
        json cpuData;
        std::ifstream statFile("/proc/stat");
        std::string line;
        if (std::getline(statFile, line)) {
            if (line.substr(0, 3) == "cpu") {
                std::istringstream iss(line);
                std::vector<std::string> tokens{
                    std::istream_iterator<std::string>{iss},
                    std::istream_iterator<std::string>{}
                };

                if (tokens.size() >= 8) {
                    unsigned long long user = std::stoull(tokens[1]);
                    unsigned long long nice = std::stoull(tokens[2]);
                    unsigned long long system = std::stoull(tokens[3]);
                    unsigned long long idle = std::stoull(tokens[4]);
                    unsigned long long iowait = std::stoull(tokens[5]);
                    unsigned long long irq = std::stoull(tokens[6]);
                    unsigned long long softirq = std::stoull(tokens[7]);

                    unsigned long long total = user + nice + system + idle + iowait + irq + softirq;
                    unsigned long long nonIdle = user + nice + system + irq + softirq;

                    static unsigned long long prevTotal = 0;
                    static unsigned long long prevNonIdle = 0;
                    if (prevTotal != 0) {
                        unsigned long long totalDiff = total - prevTotal;
                        unsigned long long nonIdleDiff = nonIdle - prevNonIdle;
                        double usage = (totalDiff > 0) ? (100.0 * nonIdleDiff / totalDiff) : 0.0;
                        cpuData["total"] = usage;
                    }
                    prevTotal = total;
                    prevNonIdle = nonIdle;
                }
            }
        }

        for (int coreId : coreIds) {
            std::string corePrefix = "cpu" + std::to_string(coreId);
            statFile.clear();
            statFile.seekg(0);

            while (std::getline(statFile, line)) {
                if (line.substr(0, corePrefix.size()) == corePrefix) {
                    std::istringstream iss(line);
                    std::vector<std::string> tokens{
                        std::istream_iterator<std::string>{iss},
                        std::istream_iterator<std::string>{}
                    };

                    if (tokens.size() >= 8) {
                        unsigned long long user = std::stoull(tokens[1]);
                        unsigned long long nice = std::stoull(tokens[2]);
                        unsigned long long system = std::stoull(tokens[3]);
                        unsigned long long idle = std::stoull(tokens[4]);
                        unsigned long long iowait = std::stoull(tokens[5]);
                        unsigned long long irq = std::stoull(tokens[6]);
                        unsigned long long softirq = std::stoull(tokens[7]);

                        unsigned long long total = user + nice + system + idle + iowait + irq + softirq;
                        unsigned long long nonIdle = user + nice + system + irq + softirq;

                        static std::unordered_map<int, unsigned long long> prevCoreTotals;
                        static std::unordered_map<int, unsigned long long> prevCoreNonIdles;
                        
                        if (prevCoreTotals.find(coreId) != prevCoreTotals.end()) {
                            unsigned long long totalDiff = total - prevCoreTotals[coreId];
                            unsigned long long nonIdleDiff = nonIdle - prevCoreNonIdles[coreId];
                            double usage = (totalDiff > 0) ? (100.0 * nonIdleDiff / totalDiff) : 0.0;
                            cpuData["cores"][std::to_string(coreId)] = usage;
                        }
                        
                        prevCoreTotals[coreId] = total;
                        prevCoreNonIdles[coreId] = nonIdle;
                    }
                    break;
                }
            }
        }
        return cpuData;
    }

    //Получение метрик памяти
    json getMemoryMetrics(const std::vector<std::string>& specs) {
        json memoryData;
        std::ifstream meminfoFile("/proc/meminfo");
        std::string line;

        std::unordered_map<std::string, unsigned long long> memValues;

        while (std::getline(meminfoFile, line)) {
            std::istringstream iss(line);
            std::string key;
            unsigned long long value;
            std::string unit;

            iss >> key >> value >> unit;

            if (!key.empty() && key.back() == ':') {
                key.pop_back();
            }

            memValues[key] = value;
        }

        for (const auto& spec : specs) {
            if (spec == "used") {
                if (memValues.count("MemTotal") && memValues.count("MemFree")) {
                    unsigned long long used = memValues["MemTotal"] - memValues["MemFree"];
                    memoryData["used"] = used;
                }
            } else if (spec == "free") {
                if (memValues.count("MemFree")) {
                    memoryData["free"] = memValues["MemFree"];
                }
            } else if (spec == "available") {
                if (memValues.count("MemAvailable")) {
                    memoryData["available"] = memValues["MemAvailable"];
                }
            } else if (spec == "cached") {
                if (memValues.count("Cached")) {
                    memoryData["cached"] = memValues["Cached"];
                }
            } else if (spec == "buffers") {
                if (memValues.count("Buffers")) {
                    memoryData["buffers"] = memValues["Buffers"];
                }
            }
        }

        return memoryData;
    }

    //Вывод метрик
    void outputMetrics(const json& metricsData) {
        for (const auto& output : outputs_) {
            if (output.type == "console") {
                outputToConsole(metricsData);
            } else if (output.type == "file") {
                outputToFile(metricsData, output.path);
            }
        }
    }

    //Вывод в консоль
    void outputToConsole(const json& metricsData) {
        std::cout << "System Metrics at " << metricsData["timestamp"].get<std::string>() << ":\n";

        if (metricsData.contains("cpu")) {
            const auto& cpuData = metricsData["cpu"];
            std::cout << "CPU Usage:\n";
            if (cpuData.contains("total")) {
                std::cout << "  Total: " << std::fixed << std::setprecision(2)
                          << cpuData["total"].get<double>() << "%\n";
            }
            if (cpuData.contains("cores")) {
                for (const auto& [core, usage] : cpuData["cores"].items()) {
                    std::cout << "  Core " << core << ": " << std::fixed << std::setprecision(2) 
                              << usage.get<double>() << "%\n";
                }
            }
        }

        if (metricsData.contains("memory")) {
            const auto& memoryData = metricsData["memory"];
            std::cout << "Memory Usage (MB):\n";
            for (const auto& [key, value] : memoryData.items()) {
                std::cout << "  " << key << ": " << (value.get<unsigned long long>() / 1024) << "\n";
            }
        }

        std::cout << std::endl;
    }

    //Вывод в файл логов
    void outputToFile(const json& metricsData, const std::string& path) {
        try {
            std::ofstream outFile;
            if (fs::exists(path)) {
                outFile.open(path, std::ios::app);
            } else {
                outFile.open(path);
                outFile << "timestamp,metric_type,metric_key,metric_value\n";
            }

            if (!outFile.is_open()) {
                std::cerr << "Error opening output file: " << path << std::endl;
                return;
            }

            std::string timestamp = metricsData["timestamp"].get<std::string>();

            if (metricsData.contains("cpu")) {
                const auto& cpuData = metricsData["cpu"];
                if (cpuData.contains("total")) {
                    outFile << timestamp << ",cpu,total,"
                            << cpuData["total"].get<double>() << "\n";
                }
                if (cpuData.contains("cores")) {
                    for (const auto& [core, usage] : cpuData["cores"].items()) {
                        outFile << timestamp << ",cpu,core_" << core << ","
                                << usage.get<double>() << "\n";
                    }
                }
            }

            if (metricsData.contains("memory")) {
                const auto& memoryData = metricsData["memory"];
                for (const auto& [key, value] : memoryData.items()) {
                    outFile << timestamp << ",memory," << key << ","
                            << (value.get<unsigned long long>() / 1024) << "\n";
                }
            }

        } catch (const std::exception& e) {
            std::cerr << "Ошибка записи в файл: " << e.what() << std::endl;
        }
    }

    //Получение даты в строке
    std::string getCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);

        std::stringstream ss;
        ss << std::put_time(std::localtime(&in_time_t), "%d-%m-%Y %H:%M:%S");
        return ss.str();
    }
};

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Писать вот так: " << argv[0] << " <config_file>" << std::endl;
        return 1;
    }

    try {
        SystemMonitor monitor(argv[1]);
        monitor.start();

        std::cout << "Мониторинг запущен. Нажмите Enter для остановки..." << std::endl;
        std::cin.ignore();

        monitor.stop();
        std::cout << "Мониторинг остановлен." << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Ошибка: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

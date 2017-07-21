#include <iostream>
#include <regex>
#include <fstream>

#include "json.hpp"
using json = nlohmann::json;

// Data structures for holding the parsed data

// A single run of a single function with a certain template type
struct bench_run {
  long real_time = -1;
  long cpu_time = -1;

  bench_run(long real_time, long cpu_time)
  : real_time(real_time), cpu_time(cpu_time)
  {
  }
  bench_run() = default;
};

// Series of runs for the same function with the same template type
struct bench_series {
  std::map<long, bench_run> mean_run_by_input_size;
  std::map<long, bench_run> stddev_run_by_input_size;
};

// All series of this function with their template types 
struct bench_func {
  std::map<std::string, bench_series> series_by_template_type;
};

int main(int argc, char** argv) {
  if (argc <= 2) {
    std::cerr << "Invoke program like this: "
              << argv[0] << " input.json" << std::endl;
    return 1;
  }
  // read the input JSON file into j
  std::ifstream i(argv[1]);
  json j;
  i >> j;

  // List of all the functions we benchmarked 
  std::map<std::string, bench_func> funcs;

  std::regex mean_regex(R"regex(([A-Za-z_0-9:]+)[<]([A-Za-z_0-9:]+)[>]/(\d+)_mean)regex");
  std::regex stddev_regex(R"regex(([A-Za-z_0-9:]+)[<]([A-Za-z_0-9:]+)[>]/(\d+)_stddev)regex");
  std::smatch base_match;

  for(auto& b : j["benchmarks"]) {
    std::string name = b["name"];
    long real_time = b["real_time"];
    long cpu_time = b["cpu_time"];
    std::string template_arg;
    std::string func;
    long input_size;
    bool is_mean = false;
    bool is_stddev = false;
    if (std::regex_match(name, base_match, mean_regex)) {
      is_mean = true;
    } else if (std::regex_match(name, base_match, stddev_regex)) {
      is_stddev = true;
    } else {
      std::cerr << "couldnt match name " << name << std::endl;
      continue;
    }

    if (base_match.size() == 4) {
      func = base_match[1].str();
      template_arg = base_match[2].str();
      input_size = std::atol(base_match[3].str().c_str());
      auto & series = funcs[func].series_by_template_type[template_arg];
      if (is_mean) {
        series.mean_run_by_input_size[input_size] = bench_run(real_time, cpu_time);
      } else if (is_stddev) {
        series.stddev_run_by_input_size[input_size] = bench_run(real_time, cpu_time);
      } else {
        assert(false);
      }
    } else {
      std::cerr << "not enough results in match?" << std::endl;
    }
  }


  for (auto& func_pair : funcs) {
    json output = {
      {"chart", {
        {"zoomType" , "x"}
      }},
      {"title" , {
        {"text" , "Vectorization " + func_pair.first}
      }},
      {"yAxis" , {
          {"labels" , {
            {"format" , "{value}ms"}
          }},
          {"title", {"text" , "real time"}}
      }},

      {"tooltip", {
        {"shared", true}
      }},
    };
    
    json series_list = json::array();
    for (auto& series : func_pair.second.series_by_template_type) {
      json mean_run = {
        {"name", "Time " + series.first},
        {"type", "spline"},
        {"data", json::array()},
        {"marker", {
            {"enabled", false}
        }},
      };
      json stddev_run = {
        {"color", "#FF0000"},
        {"name", "Time error " + series.first},
        {"type", "errorbar"},
        {"data", json::array()},
        {"tooltip", {
            "pointFormat", "Error range: {point.low}-{point.high}°C"
        }},
        {"stemWidth", 3},
        {"whiskerLength", 0}
      };
      for (auto& run : series.second.mean_run_by_input_size) {
        mean_run["data"] += run.second.real_time;
      }
      for (auto& run : series.second.stddev_run_by_input_size) {
        auto mean_time = series.second.mean_run_by_input_size[run.first].real_time;
        stddev_run["data"] += json::array({mean_time - run.second.real_time, mean_time + run.second.real_time});
      }
      series_list += mean_run;
      series_list += stddev_run;
    }
    output["series"] = series_list;
    
    //std::cout << output.dump(1) << std::endl;
    // write prettified JSON to another file
    std::ofstream o(func_pair.first + ".vec.js");
    o << "Highcharts.chart('container', ";
    o << std::setw(1) << output << ");\n";
  }

}

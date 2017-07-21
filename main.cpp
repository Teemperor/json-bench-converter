#include <iostream>
#include <regex>
#include <fstream>

#include "json.hpp"
using json = nlohmann::json;

// Data structures for holding the parsed data

// A single run of a single function with a certain template type
struct bench_run {
  std::map<std::string, long> times;

  bench_run(long real_time, long cpu_time)
  {
    times["real"] = real_time;
    times["CPU"] = cpu_time;
  }
  bench_run() = default;
};

// Series of runs for the same function with the same template type.
// Input size is either this funny integer we have in front of "_mean"
// or the number of threads
struct bench_series {
  std::map<long, bench_run> mean_run_by_input_size;
  std::map<long, bench_run> stddev_run_by_input_size;
};

// All series of this function with their template types 
struct bench_func {
  std::map<std::string, bench_series> series_by_template_type;
};

// Goes through all nodes and creates javascript files for stuff that looks like vectorized code
void handle_vectorize_cases(json j) {
  
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
      std::cerr << "couldnt match name for vec regex: " << name << std::endl;
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
    std::string function_name = func_pair.first;
    json output = {
      {"chart", {
        {"zoomType" , "x"}
      }},
      {"title" , {
        {"text" , "Vectorization " + function_name}
      }},
      {"yAxis" , {
          {"labels" , {
            {"format" , "{value}ms"}
          }},
          {"title", {"text" , "time"}}
      }},

      {"tooltip", {
        {"shared", true}
      }},
    };
    
    json series_list = json::array();
    
    std::vector<std::string> time_kinds = {"real", "CPU"};
    // We want one data row for real and one for CPU time.
    // The strings are taken to lookup in the map of bench_run.
    for (std::string time_kind : time_kinds) {
      for (auto& series : func_pair.second.series_by_template_type) {
        std::string template_type = series.first;
        json mean_run = {
          {"name", time_kind + " time " + template_type},
          {"type", "spline"},
          {"data", json::array()},
          {"marker", {
              {"enabled", false}
          }},
        };
        json stddev_run = {
          {"color", "#FF0000"},
          {"name", time_kind + " time error " + template_type},
          {"type", "errorbar"},
          {"data", json::array()},
          {"tooltip", {
              "pointFormat", "Error range: {point.low}-{point.high}ms"
          }},
          {"stemWidth", 3},
          {"whiskerLength", 0}
        };
        for (auto& run : series.second.mean_run_by_input_size) {
          mean_run["data"] += run.second.times[time_kind];
        }
        for (auto& run : series.second.stddev_run_by_input_size) {
          // The stddev is relative, so we have to get the mean time and then add/substract
          // our stddev time
          auto mean_time = series.second.mean_run_by_input_size[run.first].times[time_kind];
          auto stddev_time = run.second.times[time_kind];
          stddev_run["data"] += json::array({mean_time - stddev_time, mean_time + stddev_time});
        }
        series_list += mean_run;
        series_list += stddev_run;
      }
    }
    output["series"] = series_list;
    
    //std::cout << output.dump(1) << std::endl;
    // write prettified JSON to another file
    std::ofstream o(function_name + ".vec.js");
    o << "Highcharts.chart('container', ";
    o << std::setw(1) << output << ");\n";
  }
}

// Goes through all nodes and creates javascript files for things that look like
// threading benchmarks.
void handle_threading_cases(json j) {
  
  // List of all the functions we benchmarked 
  std::map<std::string, bench_series> funcs;

  std::regex thread_regex(R"regex(([A-Za-z_0-9:]+)/threads:(\d+))regex");
  std::smatch base_match;

  // Build up our data structure "funcs"
  for(auto& b : j["benchmarks"]) {
    std::string name = b["name"];
    long real_time = b["real_time"];
    long cpu_time = b["cpu_time"];
    std::string template_arg;
    std::string func;
    long threads;
    
    if (!std::regex_match(name, base_match, thread_regex)) {
      std::cerr << "couldnt match name for thread regex: " << name << std::endl;
      continue;
    }

    if (base_match.size() == 3) {
      func = base_match[1].str();
      threads = std::atol(base_match[2].str().c_str());
      auto & series = funcs[func];
      series.mean_run_by_input_size[threads] = bench_run(real_time, cpu_time);
    } else {
      std::cerr << "not enough results in match?" << std::endl;
    }
  }

  // Foreach function, create a javascript file
  for (auto& func_pair : funcs) {
    std::string func_name = func_pair.first;
    // header of the JSON object with generic information
    json output = {
      {"chart", {
        {"zoomType" , "x"}
      }},
      {"title" , {
        {"text" , "Threading " + func_name}
      }},
      {"yAxis" , {
          {"labels" , {
            {"format" , "{value}ms"}
          }},
          {"title", {"text" , "time"}}
      }},

      {"tooltip", {
        {"shared", true}
      }},
    };
    
    json series_list = json::array();
    
    std::vector<std::string> time_kinds = {"real", "CPU"};
    // We want one data row for real and one for CPU time.
    // The strings are taken to lookup in the map of bench_run.
    for (std::string time_kind : time_kinds) {
      json mean_run = {
        {"name", time_kind + " time " + func_name},
        {"type", "spline"},
        {"data", json::array()},
        {"marker", {
            {"enabled", false}
        }},
      };
      for (auto& run : func_pair.second.mean_run_by_input_size) {
        mean_run["data"] += run.second.times[time_kind];
      }
      series_list += mean_run;
    }
    output["series"] = series_list;
    
    //std::cout << output.dump(1) << std::endl;
    // Write the whole thing to a javascript file.
    std::ofstream o(func_name + ".thread.js");
    // Write the 'output' json object to this file with some
    // code around that makes it valid javascript.
    o << "Highcharts.chart('container', ";
    o << std::setw(1) << output << ");\n";
  }
}

int main(int argc, char** argv) {
  if (argc <= 1) {
    std::cerr << "Invoke program like this: "
              << argv[0] << " input.json" << std::endl;
    return 1;
  }
  // read the input JSON file into j
  std::ifstream i(argv[1]);
  json j;
  i >> j;
  // Create vectorize/threading JavaScript files
  handle_vectorize_cases(j);
  handle_threading_cases(j);

}

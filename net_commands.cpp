#include <functional>
#include <ctime>
#include <chrono>
#include <fstream>
#include <iostream>
#include <exception>
#include <vector>

#include "csv.h"
#include "json.hpp"
#include "insp_session.hpp"

using namespace std::chrono_literals;

struct tube {
	std::string x_label;
	std::string y_label;
	float cl_x;
	float cl_y;
	float hl_x;
	float hl_y;
	int tube_id;
	std::string color = "white";
	std::string insp_plan = "";
};

void to_json(nlohmann::json &j, const tube &t) {
	j = nlohmann::json { { "x_label", t.x_label }, { "y_label", t.y_label }, {
			"cl_x", t.cl_x }, { "cl_y", t.cl_y }, { "hl_x", t.hl_x }, { "hl_y",
			t.hl_y }, { "tube_id",	t.tube_id }, };
}

void from_json(const nlohmann::json &j, tube &t) {
	j.at("x_label").get_to(t.x_label);
	j.at("y_label").get_to(t.y_label);
	j.at("cl_x").get_to(t.cl_x);
	j.at("cl_y").get_to(t.cl_y);
	j.at("hl_x").get_to(t.hl_x);
	j.at("hl_y").get_to(t.hl_y);
	j.at("tube_id").get_to(t.tube_id);
}

nlohmann::json hx_load_tubesheet_cmd(nlohmann::json pars) {
	// Parse the CSV file to extract the data for each tube
	std::vector<tube> tubes;
	io::CSVReader<7, io::trim_chars<' ', '\t'>, io::no_quote_escape<';'>> in(
			"tubesheet.csv");
	in.read_header(io::ignore_extra_column, "x_label", "y_label", "cl_x",
			"cl_y", "hl_x", "hl_y", "tube_id");
	std::string x_label, y_label;
	float cl_x, cl_y, hl_x, hl_y;
	std::string tube_id;
	while (in.read_row(x_label, y_label, cl_x, cl_y, hl_x, hl_y, tube_id)) {
		tubes.push_back( { x_label, y_label, cl_x, cl_y, hl_x, hl_y, std::stoi(tube_id.substr(5))});
	}

	nlohmann::json res(tubes); //requires to_json and from_json to be defined to be able to serialize the custom object "tube"
	return res;
}

nlohmann::json hx_list_cmd(nlohmann::json pars) {
	nlohmann::json res;

	std::string path = "./HXs";
	for (const auto &entry : std::filesystem::directory_iterator(path)) {
		if (entry.is_directory()) {
			res.push_back( { { "name", entry.path().filename() }, { "abreb",
					entry.path().filename() } });
		}
	}
	std::sort(res.begin(), res.end());

	return res;
}

namespace fs = std::filesystem;

template<typename TP>
std::time_t to_time_t(TP tp) {
	using namespace std::chrono;
	auto sctp = time_point_cast<system_clock::duration>(
			tp - TP::clock::now() + system_clock::now());
	return system_clock::to_time_t(sctp);
}

nlohmann::json insp_sessions_list_cmd(nlohmann::json pars) {
	nlohmann::json res;

	std::string path = "./insp_sessions/";
	for (const auto &entry : std::filesystem::directory_iterator(path)) {
		if (entry.is_regular_file()) {

			std::time_t tt = to_time_t(entry.last_write_time());
			std::tm *gmt = std::gmtime(&tt);
			std::stringstream buffer;
			buffer << std::put_time(gmt, "%A, %d %B %Y %H:%M");
			std::string formattedFileTime = buffer.str();

			//res.push_back( std::make_pair(std::make_pair( "file_name", entry.path().filename()), std::make_pair(
			//		"file_date", formattedFileTime)));
			std::string filename = entry.path().filename();
			res.push_back(
					nlohmann::json { { "file_name", filename }, { "file_date",
							formattedFileTime },
			});
		}
	}
	std::sort(res.begin(), res.end());

	return res;
}

extern inspection_session current_session;


nlohmann::json session_create_cmd(nlohmann::json pars) {
	std::string session_name = pars["session_name"];

	nlohmann::json res;

	std::filesystem::path inspection_session_file = session_name;
	if (inspection_session_file.empty() || ! inspection_session_file.has_filename()) {
		res["success"] = false;
		res["logs"] = "no filename specified";
		return res;
	}

	if (pars["hx"].empty()) {
		res["success"] = false;
		res["logs"] = "no HX selected";
		return res;
	}

	try {
		std::filesystem::path hx_directory = std::filesystem::path("HXs").append(std::string(pars["hx"]));

		std::filesystem::path tubesheet_csv = hx_directory;
		tubesheet_csv /= "tubesheet.csv";

		std::filesystem::path tubesheet_svg = hx_directory;
		tubesheet_svg /= "tubesheet.svg";

		inspection_session new_session(inspection_session_file, hx_directory, tubesheet_csv, tubesheet_svg);
		res["logs"] = new_session.load_plans();

		std::filesystem::path complete_inspection_session_file_path = std::filesystem::path("insp_sessions").append(session_name + ".json");
		std::ofstream session(complete_inspection_session_file_path);
		session << nlohmann::json(new_session);
		current_session = new_session;
		res["success"] = true;
		return res;

	} catch (const std::exception &e) {
		res["success"]= false;
		res["logs"]= e.what();
	}
	return res;
}

nlohmann::json session_load_cmd(nlohmann::json pars) {
	std::string session_name = pars["session_name"];
	nlohmann::json res;

	if (session_name.empty()) {
		res["success"] = false;
		res["logs"] = "no session selected";
		return res;
	}

	current_session.load(std::filesystem::path("insp_sessions").append(session_name));
	res["success"] = true;
	return res;
}

nlohmann::json session_info_cmd(nlohmann::json pars) {
	nlohmann::json res = nlohmann::json(nlohmann::json::value_t::object);

	if (current_session.is_loaded()) {
		res["tubesheet_svg_path"] = current_session.tubesheet_svg;
		res["last_selected_plan"] = current_session.get_selected_plan();

		std::vector<std::string> insp_plans;
		for (auto &i_p : current_session.insp_plans) {
			insp_plans.push_back(i_p.first);
		}

		res["inspection_plans"] = insp_plans;
	}

	return res;
}


std::map<std::string, std::function<nlohmann::json(nlohmann::json)>> commands =
		{{ "hx_list", &hx_list_cmd },
		 { "hx_load_tubesheet", &hx_load_tubesheet_cmd },
		 { "insp_sessions_list", &insp_sessions_list_cmd },
		 { "session_create", &session_create_cmd },
		 { "session_load", &session_load_cmd },
		 { "session_info", &session_info_cmd },
		};

nlohmann::json cmd_execute(std::string command, nlohmann::json par) {
	nlohmann::json res;
	if (auto cmd_entry = commands.find(command); cmd_entry != commands.end()) {
		res = (*cmd_entry).second(par);
	}
	return res;
}

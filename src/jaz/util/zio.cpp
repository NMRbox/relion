#include "zio.h"

std::vector<double> ZIO::readDoubles(std::string fn)
{
	std::vector<double> out(0);
	std::ifstream ifs(fn);	
	std::string line;
	
	out.reserve(128);
	
	while (std::getline(ifs, line))
	{
		std::stringstream sts;
		sts << line;
		
		double d;
		sts >> d;
		
		out.push_back(d);
	}
	
	return out;
}

std::vector<std::vector<double>> ZIO::readDoublesTable(std::string fn, int cols, char delim)
{
	std::vector<std::vector<double>> out(0);
	
	std::ifstream ifs(fn);

	if (!ifs)
	{
		REPORT_ERROR_STR("ZIO::readDoublesTable: unable to read "+fn);
	}
	
	std::string line;
	
	out.reserve(128);
	
	int i = -1;
	
	while (std::getline(ifs, line))
	{
		out.push_back(std::vector<double>(cols, 0.0));
		i++;

		if (delim != ' ')
		{
			for (int i = 0; i < line.length(); i++)
			{
				if (line[i] == delim)
				{
					line[i] = ' ';
				}
			}
		}
		
		std::stringstream sts;
		sts << line;
		
		for (int c = 0; c < cols; c++)
		{
			double d;
			sts >> d;
			
			out[i][c] = d;
		}
	}
	
	return out;
}

std::string ZIO::itoa(double num)
{
	std::ostringstream sts;
	sts << num;
	
	return sts.str();
}

bool ZIO::beginsWith(const std::string &string, const std::string &prefix)
{
	return string.length() >= prefix.length() 
			&& string.substr(0, prefix.length()) == prefix;
}

bool ZIO::endsWith(const std::string &string, const std::string &prefix)
{
	return string.length() >= prefix.length() 
			&& string.substr(string.length() - prefix.length()) == prefix;
}

std::string ZIO::makeOutputDir(const std::string& dir)
{
	std::string out = dir;
	
	const int len = out.length();
	
	if (len > 0)
	{
		if (out[out.length()-1] != '/')
		{
			out = out + "/";
		}
		
		int res = system(("mkdir -p "+out).c_str());
	}
	
	return out;
}

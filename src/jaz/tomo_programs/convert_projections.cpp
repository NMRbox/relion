#include "convert_projections.h"
#include <src/jaz/tomo/projection/projection.h>
#include <src/jaz/tomo/extraction.h>
#include <src/jaz/util/image_file_helper.h>

#include <omp.h>

using namespace gravis;

void ConvertProjectionsProgram::run()
{
	t3Vector<long int> stackSize = ImageFileHelper::getSize(tsFn);
	const int w_ts = stackSize.x;
	const int h_ts = stackSize.y;
	const int fc_ts = stackSize.z;
	
	
	std::string origStackFn, aliFn, xfFn;
	int w_ali_bin = -1, h_ali_bin = -1;
	double binning_ali = 1.0;
	
	readNewstCom(newstComFn, origStackFn, aliFn, xfFn, w_ali_bin, h_ali_bin, binning_ali);
	
	double fileThickness, fileBinning = 1.0;
	double w0 = -1.0, h0 = -1.0;
	std::string tltFn, aliFnTilt;
	
	std::vector<bool> exclude(fc_ts, false);
	
	double imodShiftX(0), imodShiftY(0);
	
	readTiltCom(tltComFn, aliFnTilt, tltFn, w0, h0, fileThickness, 
				fileBinning, exclude, imodShiftX, imodShiftY);
	
	int excluded = 0;
	for (int i = 0; i < fc_ts; i++)
	{
		if (exclude[i]) excluded++;
	}
	
	const int fc_good = fc_ts - excluded;
	
	if (excluded > 0 && outFnCrop == "")
	{
		REPORT_ERROR_STR("Frames have been excluded in " << tltComFn << ", but no output filename "
						 << "for the culled stack has been specified (--oc).");
	}
	
	std::vector<int> oldIndex(fc_good);
	
	{
		int j = 0;
		
		if (excluded > 1) std::cout << "excluding frames: ";
		else if (excluded > 0) std::cout << "excluding frame: ";
		
		for (int i = 0; i < fc_ts; i++)
		{
			if (!exclude[i])
			{
				oldIndex[j] = i;
				j++;
			}
			else
			{
				std::cout << i << " ";
			}
		}
		
		if (excluded > 0) std::cout << std::endl;
	}
	
	if (w0 < 0.0)
	{
		REPORT_ERROR("Original image size not found in " + tltComFn + ".");
	}
	
	if (binning_ali >= 0.0 && fileBinning >= 0.0 && binning_ali != fileBinning)
	{
		std::cerr << "WARNING: different binning levels found in "
				  << newstComFn << " and " << tltComFn << std::endl;
	}
	
	if (aliFnTilt != aliFn)
	{
		std::cerr << "WARNING: different aligned stacks referenced in "
				  << newstComFn << " (" << aliFn << ") and "
				  << tltComFn << " (" << aliFnTilt << ")" << std::endl;
	}
	
	const int w_ali = aliSize? w_ali_bin * binning_ali : w0;
	const int h_ali = aliSize? h_ali_bin * binning_ali : h0;
	
	const double thickness = (thicknessCmd < 0.0)? fileThickness : thicknessCmd;
	
	gravis::d2Vector origCenter((w_ts - 1.0) / 2.0, (h_ts - 1.0) / 2.0);
	gravis::d2Vector aliCenter((w_ali - 1.0) / 2.0, (h_ali - 1.0) / 2.0);
	
	std::vector<d4Matrix> tiltProjs = loadTiltProjections(
				tltFn, aliCenter.x, aliCenter.y, flipAngles);
	
	std::vector<d4Matrix> affineXforms = loadInvAffineTransforms(
				xfFn, ali? aliCenter : origCenter, aliCenter, 1.0, ali);
	
	if (affineXforms.size() != fc_ts)
	{
		REPORT_ERROR_STR("Mismatched frame counts between "
						 << tltFn << " and " << xfFn << ": "
						 << fc_ts << " (" << fc_good << " + " << excluded << " excl.) vs. "
						 << affineXforms.size());
	}
	
	std::vector<d4Matrix> worldToImage(fc_good);
	
	int w_out, h_out, d_out;
	
	w_out = aliSize? w_ali : w_ts;
	
	d4Matrix toImodOrigin3D = d4Matrix
	(
		1, 0, 0,      -1,
		0, 1, 0,  -thickness/2.0,
		0, 0, 1,      -1,
		0, 0, 0, 1                   );
	
	d4Matrix YzFlip;
	YzFlip.loadIdentity();
	
	if (flipYZ)
	{
		
		if (flipZ)
		{
			YzFlip = d4Matrix
			(
				1, 0, 0, 0,
				0, 0,-1, thickness-1,
				0, 1, 0, 0,
				0, 0, 0, 1  );
		}
		else
		{
			YzFlip = d4Matrix
			(
				1, 0, 0, 0,
				0, 0, 1, 0,
				0, 1, 0, 0,
				0, 0, 0, 1  );
		}
		
		h_out = aliSize? h_ali : h_ts;
		d_out = thickness;
	}
	else
	{
		h_out = thickness;
		d_out = aliSize? h_ali : h_ts;
	}
	
	d4Matrix Off;
	Off.loadIdentity();
	
	Off(0,3) += offset3Dx - imodShiftX;
	Off(1,3) += offset3Dy - imodShiftY;
	Off(2,3) += offset3Dz;
	
	if (tiltProjs.size() != fc_good)
	{
		std::cerr << "WARNING: the file " << tltFn << " contains " << tiltProjs.size() 
				  << " tilt angles, although frames have been excluded.\n";
		
		if (tiltProjs.size() == fc_ts)
		{
			std::cerr << "Assuming old frame number.\n";
			
			for (int f = 0; f < fc_good; f++)
			{
				worldToImage[f] = 
					  affineXforms[oldIndex[f]] 
					* tiltProjs[oldIndex[f]] 
					* toImodOrigin3D 
					* Off
					* YzFlip;
			}
		}
		else
		{
			std::cerr << "This number makes no sense - aborting.\n";
		}
	}
	else
	{
		for (int f = 0; f < fc_good; f++)
		{
			worldToImage[f] = 
				  affineXforms[f] 
				* tiltProjs[f] 
				* toImodOrigin3D 
				* Off 
				* YzFlip;
		}
	}
	
	/*if (flipYZ)
	{
		ProjectionIO::write(worldToImage, w_out, d_out, h_out, outFn);
	}
	else
	{*/
	ProjectionIO::write(worldToImage, w_out, h_out, d_out, outFn);
	//}
	
	if (outFnCrop != "")
	{
		BufferedImage<float> ts0;
		ts0.read(tsFn);
		
		const size_t sx = ts0.xdim;
		const size_t sy = ts0.ydim;
		
		BufferedImage<float> ts1(sx,sy,fc_good);
		
		for (int f = 0; f < fc_good; f++)
		{
			std::cout << f << " <- " << oldIndex[f] << "\n";
			
			for (size_t y = 0; y < sy; y++)
			for (size_t x = 0; x < sx; x++)
			{
				ts1(x,y,f) = ts0(x,y,oldIndex[f]);
			}
		}
	}
}

void ConvertProjectionsProgram::readNewstCom(
		std::string filename, 
		std::string& origStackFn, std::string& aliFn, std::string& xfFn, 
		int& w_ali, int& h_ali, 
		double& binning_ali)
{
	std::ifstream newstComFile(filename);
	
	if (!newstComFile.is_open())
	{
		REPORT_ERROR("Unable to read: " + filename);
	}
	
	char text[4096];
	const std::string inKey = "InputFile";
	const std::string outKey = "OutputFile";
	const std::string xfKey = "TransformFile";
	const std::string sizeKey = "SizeToOutputInXandY";
	const std::string binningKey = "BinByFactor";
	std::string key;
	
	while (newstComFile.good())
	{
		newstComFile.getline(text, 4096);
		
		if (strlen(text) < 5) continue;
		
		std::istringstream line(text);
		
		line >> key;
		
		if (key == inKey)
		{			
			line >> origStackFn;
		}
		else if (key == outKey)
		{			
			line >> aliFn;
		}
		else if (key == xfKey)
		{			
			line >> xfFn;
		}
		else if (key == sizeKey)
		{			
			std::string vec;
			line >> vec;
			
			for (int i = 0; i < vec.length(); i++)
			{
				if (vec[i] == ',') vec[i] = ' ';
			}
			
			std::istringstream iss(vec);
			
			iss >> w_ali;
			iss >> h_ali;
		}
		else if (key == binningKey)
		{			
			line >> binning_ali;
		}
	}
}

void ConvertProjectionsProgram::readTiltCom(
		std::string filename, 
		std::string& aliFn,
		std::string& tltFn,
		double& w, double& h, 
		double& thickness, double& binning,
		std::vector<bool>& exclude,
		double& shiftX,
		double& shiftZ)
{
	w = -1;
	h = -1;
	thickness = -1.0;
	binning = -1.0;
	
	const int fc0 = exclude.size();
	
	std::ifstream tiltComFile(filename);
	
	if (!tiltComFile.is_open())
	{
		REPORT_ERROR("Unable to read: " + filename);
	}
	
	char text[4096];
	const std::string aliKey = "InputProjections";
	const std::string sizeKey = "FULLIMAGE";
	const std::string tltKey = "TILTFILE";
	const std::string thicknessKey = "THICKNESS";
	const std::string binningKey = "IMAGEBINNED";
	const std::string excludeKey = "EXCLUDELIST";
	const std::string excludeKey2 = "EXCLUDELIST2";
	const std::string excludeKey3 = "EXCLUDE";
	const std::string shiftKey = "SHIFT";
	std::string key;
	
	while (tiltComFile.good())
	{
		tiltComFile.getline(text, 4096);
		
		if (strlen(text) < 5) continue;
		
		std::stringstream line(text);
		
		line >> key;
		
		if (key == aliKey)
		{			
			line >> aliFn;
		}
		else if (key == thicknessKey)
		{			
			line >> thickness;
		}
		else if (key == tltKey)
		{
			line >> tltFn;
		}
		else if (key == binningKey)
		{			
			line >> binning;
		}
		else if (key == sizeKey)
		{
			line >> w;
			line >> h;
		}
		else if (key == shiftKey)
		{
			line >> shiftX;
			line >> shiftZ;
		}
		else if (key == excludeKey || key == excludeKey2 || key == excludeKey3)
		{
			std::string list;
			line >> list;
			
			std::string currNum = "";
			bool parsingRange = false;
			int range0 = 0;
			
			for (int i = 0; i < list.size()+1; i++)
			{
				if (i < list.size() && list[i] >= '0' && list[i] <= '9')
				{
					currNum += list[i];
				}
				else if (i == list.size() || list[i] >= ' ' || list[i] <= ',')
				{
					if (parsingRange)
					{
						std::istringstream sts(currNum);
						int range1;
						sts >> range1;
						currNum = "";
						
						if (range1 < 1 || range1 > fc0)
						{
							REPORT_ERROR_STR("Bad exclusion index (" 
											 << range1 << ") in " << filename);
						}
						
						for (int x = range0; x <= range1; x++)
						{
							exclude[x-1] = true;
						}
						
						parsingRange = false;
					}
					else
					{
						std::istringstream sts(currNum);
						int x;
						sts >> x;
						currNum = "";
						
						if (x < 1 || x > fc0)
						{
							REPORT_ERROR_STR("Bad exclusion index (" 
											 << x << ") in " << filename);
						}
						
						exclude[x-1] = true;
					}
				}
				else if (list[i] == '-')
				{
					std::istringstream sts(currNum);
					sts >> range0;
					currNum = "";
					
					if (range0 < 1 || range0 > fc0)
					{
						REPORT_ERROR_STR("Bad exclusion range index (" 
										 << range0 << ") in " << filename);
					}
					
					parsingRange = true;
				}				
			}
		}
	}
}

std::vector<d4Matrix> ConvertProjectionsProgram::loadInvAffineTransforms(
		std::string xformFile, d2Vector centerOrig, d2Vector centerAli, 
		double binning, bool forAli)
{
	std::ifstream file(xformFile.c_str());
	
	if (!file.is_open())
	{
		REPORT_ERROR("failed to open " + xformFile + '\n');
	}
	
	std::vector<d4Matrix> xforms;
	
	d4Matrix P, Q;
	P.loadIdentity();
	Q.loadIdentity();
	
	P(0,3) = centerOrig.x;
	P(1,3) = centerOrig.y;
	
	Q(0,3) = -centerAli.x;
	Q(1,3) = -centerAli.y;
	
	char text[4096];
	
	while (file.good())
	{
		file.getline(text, 4096);
		
		if (strlen(text) < 11) break;
		
		std::stringstream line(text);
		
		d4Matrix A;
		A.loadIdentity();
		
		line >> A(0,0);
		line >> A(0,1);
		line >> A(1,0);
		line >> A(1,1);
		line >> A(0,3);
		line >> A(1,3);
		
		d4Matrix Ai = A;
		Ai.invert();
		
		d4Matrix B = forAli?  P * Q  :  P * Ai * Q;
		
		for (int r = 0; r < 3; r++)
		for (int c = 0; c < 3; c++)
		{
			B(r,c) *= binning;
		}
		
		xforms.push_back(B);
	}
	
	return xforms;
}

std::vector<gravis::d4Matrix> ConvertProjectionsProgram::loadTiltProjections(
		std::string tiltFile, 
		double centerX, double centerY, bool flipAngles)
{
	std::ifstream anglesFile(tiltFile.c_str());
	
	if (!anglesFile.is_open())
	{
		REPORT_ERROR("failed to open " + tiltFile + '\n');
	}
	
	std::vector<double> angles;
	std::vector<d4Matrix> vol2img;
	
	const double deg2rad = 3.14159265358979323846 / 180.0;
	
	d4Matrix w2i0;
	w2i0(0,3) = -centerX;
	w2i0(2,3) = -centerY;
	
	while (anglesFile.good())
	{
		double a;
		anglesFile >> a;
		
		if (!anglesFile.good()) break;
		
		a *= deg2rad;
		
		if (flipAngles) a *= -1;
		
		angles.push_back(a);
		
		d4Matrix w2i;
		
		w2i(0, 0) =  cos(a);
		w2i(0, 1) = -sin(a);
		w2i(0, 2) =  0;
		w2i(0, 3) =  centerX;
		
		w2i(1, 0) =  0;
		w2i(1, 1) =  0;
		w2i(1, 2) =  1;
		w2i(1, 3) =  centerY;
		
		w2i(2, 0) = -sin(a);
		w2i(2, 1) = -cos(a);
		w2i(2, 2) =  0;
		
		
		d4Matrix A = w2i * w2i0;
		
		vol2img.push_back(A);
	}
	
	return vol2img;
}

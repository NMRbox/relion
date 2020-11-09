#include "tomogram.h"
#include "projection_IO.h"
#include "tomo_ctf_helper.h"
#include <src/jaz/optics/damage.h>
#include <src/jaz/image/power_spectrum.h>
#include <src/jaz/tomography/particle_set.h>


using namespace gravis;


Tomogram::Tomogram()
{
	
}

double Tomogram::getFrameDose() const
{
	return cumulativeDose[frameSequence[1]] - cumulativeDose[frameSequence[0]];
}

BufferedImage<float> Tomogram::computeDoseWeight(int boxSize, double binning) const
{
	// @TODO: add support for B/k factors
	
	return Damage::weightStack_GG(cumulativeDose, optics.pixelSize * binning, boxSize);
}

BufferedImage<float> Tomogram::computeNoiseWeight(int boxSize, double binning, double overlap) const
{
	const int s0 = (int)(binning * boxSize + 0.5);
	const int s = boxSize;
	const int sh = s/2 + 1;
	const int fc = stack.zdim;

	BufferedImage<float> out(sh, s, fc);

	for (int f = 0; f < fc; f++)
	{
		BufferedImage<double> powSpec = PowerSpectrum::periodogramAverage2D(
			stack, s0, s0, overlap, f, false);

		std::vector<double> powSpec1D = RadialAvg::fftwHalf_2D_lin(powSpec);

		std::vector<float> frqWghts1D(powSpec1D.size());

		for (int i = 0; i < powSpec1D.size(); i++)
		{
			if (powSpec1D[i] > 0.0)
			{
				frqWghts1D[i] = (float)(1.0 / sqrt(powSpec1D[i]));
			}
			else
			{
				frqWghts1D[i] = 0.f;
			}
		}

		RawImage<float> outSlice = out.getSliceRef(f);
		RadialAvg::toFftwHalf_2D_lin(frqWghts1D, sh, s, outSlice, binning);
	}

	return out;
}

double Tomogram::getDepthOffset(int frame, d3Vector position) const
{
	const d4Matrix& projFrame = projectionMatrices[frame];
	d4Vector pos2D = projFrame * d4Vector(position);
	d4Vector cent2D = projFrame * d4Vector(centre);

	return pos2D.z - cent2D.z;
}

CTF Tomogram::getCtf(int frame, d3Vector position) const
{
	double dz_pos = getDepthOffset(frame, position);
	double dz = handedness * optics.pixelSize * defocusSlope * dz_pos;

	CTF ctf = centralCTFs[frame];

	ctf.DeltafU += dz;
	ctf.DeltafV += dz;

	ctf.initialise();

	return ctf;
}

int Tomogram::getLeastDoseFrame() const
{
	return IndexSort<double>::sortIndices(cumulativeDose)[0];
}

d3Vector Tomogram::computeCentreOfMass(
		const ParticleSet& particleSet,
		const std::vector<ParticleIndex>& particle_indices) const
{
	const int pc = particle_indices.size();

	d3Vector centre_of_mass(0.0, 0.0, 0.0);

	for (int p = 0; p < pc; p++)
	{
		const ParticleIndex particle_id = particle_indices[p];
		const d3Vector pos = particleSet.getPosition(particle_id);
		centre_of_mass += pos;
	}

	centre_of_mass /= pc;

	return centre_of_mass;
}

Tomogram Tomogram::extractSubstack(d3Vector position, int width, int height) const
{
	Tomogram out = *this;

	out.stack.resize(width, height, frameCount);

	for (int f = 0; f < frameCount; f++)
	{
		const d4Vector pf = projectionMatrices[f] * d4Vector(position);

		const int x0 = (int)(pf.x - width/2  + 0.5);
		const int y0 = (int)(pf.y - height/2 + 0.5);

		for (int y = 0; y < height; y++)
		for (int x = 0; x < width;  x++)
		{
			out.stack(x,y,f) = stack(x0+x, y0+y, f);
		}

		out.projectionMatrices[f](0,3) -= x0;
		out.projectionMatrices[f](1,3) -= y0;
	}

	return out;
}

Tomogram Tomogram::FourierCrop(double factor, int num_threads, bool downsampleData) const
{
	Tomogram out = *this;

	if (downsampleData && hasImage)
	{
		out.stack = Resampling::FourierCrop_fullStack(stack, factor, num_threads, true);
	}
	else
	{
		out.stack.resize(0,0,0);
		out.hasImage = false;
	}

	for (int f = 0; f < frameCount; f++)
	{
		out.projectionMatrices[f] /= factor;
	}

	out.optics.pixelSize *= factor;

	return out;
}

bool Tomogram::hasFiducials()
{
	return fiducialsFilename.length() > 0 && fiducialsFilename != "empty";
}
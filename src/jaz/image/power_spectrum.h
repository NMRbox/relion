#ifndef POWER_SPECTRUM_H
#define POWER_SPECTRUM_H

#include "raw_image.h"
#include "resampling.h"
#include "radial_avg.h"


class PowerSpectrum
{
	public:
		
		template <class T>
		static BufferedImage<T> halfReal(BufferedImage<T>& real);
		
		template <class T>
		static BufferedImage<T> halfComplex(BufferedImage<tComplex<T>>& complex);
		
		template <class T>
		static BufferedImage<T> halfComplexSqrt(BufferedImage<tComplex<T>>& complex);
		
		template <class T>
		static std::vector<double> periodogramAverage1D(
				const RawImage<T>& img,
				int s_block,
				double overlap = 2.0, int z = 0,
				bool sqRoot = false);
		
		template <class T>
		static BufferedImage<double> periodogramAverage2D(
				const RawImage<T>& img, 
				int w_block, int h_block,
				double overlap = 2.0, int z = 0,
				bool sqRoot = false);
		
		template <class T>
		static BufferedImage<T> divide(
				const RawImage<T>& img, 
				const RawImage<T>& powerSpec, 
				double eps = 1e-12);
};

template <class T>
BufferedImage<T> PowerSpectrum::halfReal(BufferedImage<T>& real)
{
	BufferedImage<tComplex<T>> complex;
	FFT::FourierTransform(real, complex, FFT::Both);
	
	return halfComplex(complex);
}

template <class T>
BufferedImage<T> PowerSpectrum::halfComplex(BufferedImage<tComplex<T>>& complex)
{
	const int wh = complex.xdim;
	const int h  = complex.ydim;
	const int d  = complex.zdim;
	
	BufferedImage<T> out(wh,h,d);
	
	for (size_t z = 0; z < d; z++)
	for (size_t y = 0; y < h; y++)
	for (size_t x = 0; x < wh; x++) 
	{
		out(x,y,z) = complex(x,y,z).norm();
	}
	
	return out;
}

template <class T>
BufferedImage<T> PowerSpectrum::halfComplexSqrt(BufferedImage<tComplex<T>>& complex)
{
	const int wh = complex.xdim;
	const int h  = complex.ydim;
	const int d  = complex.zdim;
	
	BufferedImage<T> out(wh,h,d);
	
	for (size_t z = 0; z < d; z++)
	for (size_t y = 0; y < h; y++)
	for (size_t x = 0; x < wh; x++) 
	{
		out(x,y,z) = complex(x,y,z).abs();
	}
	
	return out;
}

template <class T>
std::vector<double> PowerSpectrum::periodogramAverage1D(
		const RawImage<T>& img,
		int s_block,
		double overlap, int z,
		bool sqRoot)
{
	BufferedImage<double> pa = periodogramAverage2D(img, s_block, s_block, overlap, z, sqRoot);
	
	return RadialAvg::fftwHalf_2D_lin(pa);
}

template <class T>
BufferedImage<double> PowerSpectrum::periodogramAverage2D(
		const RawImage<T>& img, 
		int w_block, int h_block,
		double overlap, int z,
		bool sqRoot)
{
	const int w_img = img.xdim;
	const int h_img = img.ydim;
	
	const int step_x = w_block / overlap;
	const int step_y = h_block / overlap;
	
	BufferedImage<double> blockSpec(w_block, h_block), sumSpec(w_block/2+1, h_block);
	sumSpec.fill(0.0);
	
	int count = 0;
	
	BufferedImage<dComplex> clutterFS;
	
	for (int by = 0; by <= (h_img - h_block) / step_y; by++)
	for (int bx = 0; bx <= (w_img - w_block) / step_x; bx++)
	{			 
		const int origin_x = bx * step_x;
		const int origin_y = by * step_y;
				
		for (int y = 0; y < h_block; y++)
		for (int x = 0; x < w_block; x++)
		{
			blockSpec(x,y) = (double)img(origin_x + x, origin_y + y, z);
		}
				
		FFT::FourierTransform(blockSpec, clutterFS, FFT::Both);
		
		for (int y = 0; y < h_block; y++)
		for (int x = 0; x < w_block/2+1; x++)
		{
			sumSpec(x,y) += sqRoot? clutterFS(x,y).abs() : clutterFS(x,y).norm();
		}
		
		count++;
	}
	
	return sumSpec / (double)count;
}

template <class T>
BufferedImage<T> PowerSpectrum::divide(
		const RawImage<T>& img, 
		const RawImage<T>& powerSpec, 
		double eps)
{
	const int w0 = img.xdim;
	const int h0 = img.ydim;
	const int w0h = w0/2+1;
	
	BufferedImage<T> powSpecLarge = Resampling::upsampleLinear_2D_half(powerSpec, w0h, h0);
	
	const double scale = 1.0 / (powerSpec.xdim * powerSpec.ydim);
		
	BufferedImage<tComplex<T>> imgFS;
	BufferedImage<T> imgCp = img;
	FFT::FourierTransform(imgCp,imgFS);
	
	BufferedImage<tComplex<T>> outFS(w0h,h0);
	
	for (int y = 0; y < h0; y++)
	for (int x = 0; x < w0h; x++)
	{
		T s2 = powSpecLarge(x,y);
		
		if (s2 < eps) s2 = eps;
			
		outFS(x,y) = scale * imgFS(x,y) / sqrt(s2);
	}
	
	BufferedImage<T> out;
	FFT::inverseFourierTransform(outFS, out);
	
	return out;
}

#endif

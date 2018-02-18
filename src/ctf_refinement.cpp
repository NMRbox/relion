/***************************************************************************
 *
 * Authors: "Jasenko Zivanov & Sjors H.W. Scheres"
 * MRC Laboratory of Molecular Biology
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This complete copyright notice must be included in any revised version of the
 * source code. Additional authorship citations may be added, but existing
 * author citations must be preserved.
 ***************************************************************************/

#include "src/ctf_refinement.h"

void CtfRefiner::read(int argc, char **argv)
{

	parser.setCommandLine(argc, argv);
	int gen_section = parser.addSection("General options");
	// TODO: fn_opt = parser.getOption("--opt", "optimiser STAR file from a previous 3D auto-refinement");

    starFn = parser.getOption("--i", "Input STAR file");
	reconFn0 = parser.getOption("--m1", "Reference map, half 1");
    reconFn1 = parser.getOption("--m2", "Reference map, half 2");
    maskFn = parser.getOption("--mask", "Reference mask", "");
    fscFn = parser.getOption("--f", "Input STAR file with the FSC of the reference");
    outPath = parser.getOption("--o", "Output rootname, e.g. CtfRefine/job041/run");
	only_do_unfinished = parser.checkOption("--only_do_unfinished", "Skip those steps for which output files already exist.");


	// where to put these? what to do with them?
	angpix = textToFloat(parser.getOption("--angpix", "Pixel resolution (angst/pix) - read from STAR file by default", "0.0"));
    Cs = textToFloat(parser.getOption("--Cs", "Spherical aberration - read from STAR file by default", "-1"));
    kV = textToFloat(parser.getOption("--kV", "Electron energy (keV) - read from STAR file by default", "-1"));
	// In tilt_fit, these were called testtilt_x, etc. I replaced these with beamtilt_x
    // TODO: to be confirmed by Jasenko this is OK!
    beamtilt_x = textToFloat(parser.getOption("--beamtilt_x", "Beamtilt in X-direction (in mrad)", "0."));
    beamtilt_y = textToFloat(parser.getOption("--beamtilt_y", "Beamtilt in Y-direction (in mrad)", "0."));
    applyTilt = ABS(beamtilt_x) > 0. || ABS(beamtilt_y) > 0.;
    beamtilt_xx = textToFloat(parser.getOption("--beamtilt_xx", "Anisotropic beamtilt, XX-coefficient", "1."));
    beamtilt_xy = textToFloat(parser.getOption("--beamtilt_xy", "Anisotropic beamtilt, XY-coefficient", "0."));
    beamtilt_yy = textToFloat(parser.getOption("--beamtilt_yy", "Anisotropic beamtilt, YY-coefficient", "1."));

	int fit_section = parser.addSection("Defocus fit options");
	do_defocus_fit = parser.checkOption("--fit_defocus", "Perform refinement of per-particle defocus values?");
	fitAstigmatism = parser.checkOption("--astig", "Estimate independent astigmatism for each particle");
    noGlobAstig = parser.checkOption("--no_glob_astig", "Skip per-micrograph astigmatism estimation");
    diag = parser.checkOption("--diag", "Write out defocus errors");
    defocusRange = textToFloat(parser.getOption("--range", "Defocus scan range (in A)", "2000."));


	int tilt_section = parser.addSection("Beam-tilt options");
	do_tilt_fit = parser.checkOption("--fit_beamtilt", "Perform refinement of beamtilt for micrograph groups?");
    kmin = textToFloat(parser.getOption("--kmin", "Inner freq. threshold [Angst]", "30.0"));
    precomp = parser.getOption("--precomp", "Precomputed *_xy and *_w files from previous run (optional)", "");
    aniso = parser.checkOption("--anisotropic_tilt", "Use anisotropic coma model");


	int comp_section = parser.addSection("Computational options");
    nr_omp_threads = textToInteger(parser.getOption("--j", "Number of (OMP) threads", "1"));
    paddingFactor = textToFloat(parser.getOption("--pad", "Padding factor", "2"));
    minMG = textToInteger(parser.getOption("--min_MG", "First micrograph index", "0"));
    maxMG = textToInteger(parser.getOption("--max_MG", "Last micrograph index (default is to process all)", "-1"));
    imgPath = parser.getOption("--img", "Path to images - read from STAR file by default", "");
    debug = parser.checkOption("--debug", "Write debugging data");

	verb = textToInteger(parser.getOption("--verb", "Verbosity", "1"));

	// Check for errors in the command-line option
	if (parser.checkForErrors())
		REPORT_ERROR("Errors encountered on the command line (see above), exiting...");

	// Make sure fn_out ends with a slash
	if (fn_out[fn_out.length()-1] != '/')
		fn_out += "/";

}

void CtfRefiner::usage()
{
	parser.writeUsage(std::cout);
}

void CtfRefiner::initialise()
{

	//TODO: in ml_optimiser or in postprocessing job: write out some sort of 'converged' optimiser.star with all information needed.
	// Store unfiltered half-maps, run_data.star,
	// MetaDataTable MDopt, MDmodel1, MDmodel2, MDdata;
	// FileName fn_model1, fn_model2;
	// MDopt.read(fn_opt, "optimiser_general");
	// MDopt.getValue(EMDL_OPTIMISER_MODEL_STARFILE, fn_model1);
	// MDopt.getValue(EMDL_OPTIMISER_MODEL_STARFILE2, fn_model2);
	// MDmodel1.read(fn_opt, "model_classes");


	// Read in the references
	if (verb > 0)
	std::cout << " Reading references ...\n";
	maps[0].read(reconFn0);
	maps[1].read(reconFn1);

	if (maps[0].data.xdim != maps[0].data.ydim || maps[0].data.ydim != maps[0].data.zdim)
	REPORT_ERROR(reconFn0 + " is not cubical.\n");

	if (maps[1].data.xdim != maps[1].data.ydim || maps[1].data.ydim != maps[1].data.zdim)
	REPORT_ERROR(reconFn1 + " is not cubical.\n");

	if (   maps[0].data.xdim != maps[1].data.xdim
		|| maps[0].data.ydim != maps[1].data.ydim
		|| maps[0].data.zdim != maps[1].data.zdim)
		REPORT_ERROR(reconFn0 + " and " + reconFn1 + " are of unequal size.\n");

	if (maskFn != "")
	{
	if (verb > 0)
		std::cout << " Masking references ...\n";

	Image<RFLOAT> mask, maskedRef;

	mask.read(maskFn);

	ImageOp::multiply(mask, maps[0], maskedRef);
	maps[0] = maskedRef;

	ImageOp::multiply(mask, maps[1], maskedRef);
	maps[1] = maskedRef;
	}

	// dimensions
	s = maps[0].data.xdim;
	sh = s/2 + 1;


	if (verb > 0)
	std::cout << " Transforming references ...\n";

	projectors[0] = Projector(s, TRILINEAR, paddingFactor, 10, 2);
	projectors[0].computeFourierTransformMap(maps[0].data, powSpec[0].data, maps[0].data.xdim);

	if (!singleReference)
	{
	 projectors[1] = Projector(s, TRILINEAR, paddingFactor, 10, 2);
	 projectors[1].computeFourierTransformMap(maps[1].data, powSpec[1].data, maps[1].data.xdim);
	}

	useFsc = (fscFn != "");
	MetaDataTable fscMdt;

	if (useFsc)
	{
	 fscMdt.read(fscFn, "fsc");

	 if (!fscMdt.containsLabel(EMDL_SPECTRAL_IDX))
		 REPORT_ERROR(fscFn + " does not contain a value for " + EMDL::label2Str(EMDL_SPECTRAL_IDX));
	 if (!fscMdt.containsLabel(EMDL_POSTPROCESS_FSC_TRUE))
		REPORT_ERROR(fscFn + " does not contain a value for " + EMDL::label2Str(EMDL_POSTPROCESS_FSC_TRUE));

	}


	if (verb > 0)
	 std::cout << " Reading " << starFn << "...\n";

	mdt0.read(starFn);

	if (Cs < 0.0)
	{
	 mdt0.getValue(EMDL_CTF_CS, Cs, 0);
	 if (verb > 0)
		 std::cout << " + Using spherical aberration from the input STAR file: " << Cs << "\n";
	}
	else
	{
	 for (int i = 0; i < mdt0.numberOfObjects(); i++)
	 {
		 mdt0.setValue(EMDL_CTF_CS, Cs, i);
	 }
	}

	if (kV < 0.0)
	{
	 mdt0.getValue(EMDL_CTF_VOLTAGE, kV, 0);
	 if (verb > 0)
		 std::cout << " + Using voltage from the input STAR file: " << kV << " kV\n";
	}
	else
	{
	 for (int i = 0; i < mdt0.numberOfObjects(); i++)
	 {
		 mdt0.setValue(EMDL_CTF_VOLTAGE, kV, i);
	 }
	}

	if (angpix <= 0.0)
	{
	 RFLOAT mag, dstep;
	 mdt0.getValue(EMDL_CTF_MAGNIFICATION, mag, 0);
	 mdt0.getValue(EMDL_CTF_DETECTOR_PIXEL_SIZE, dstep, 0);
	 angpix = 10000 * dstep / mag;

	 if (verb > 0)
		 std::cout << " + Using pixel size calculated from magnification and detector pixel size in the input STAR file: " << angpix << "\n";
	}

	mdts = StackHelper::splitByStack(&mdt0);

	gc = maxMG >= 0? maxMG : mdts.size()-1;
	// why does g0 exist?
	g0 = minMG;

	if ((minMG > 0 || maxMG >= 0) && verb > 0)
		std::cout << " + Will only process micrographs in range: [" << g0 << "-" << gc << "] \n";


	RFLOAT V = kV * 1e3;
	lambda = 12.2643247 / sqrt(V * (1.0 + V * 0.978466e-6));

	obsModel = ObservationModel(angpix);

	if (applyTilt)
	{
	 obsModel = ObservationModel(angpix, Cs, kV * 1e3, beamtilt_x, beamtilt_y);

	 if (anisoTilt)
	 {
		 obsModel.setAnisoTilt(beamtilt_xx, beamtilt_xy, beamtilt_yy);
     }
	}


	if (useFsc)
	{
	 RefinementHelper::drawFSC(&fscMdt, freqWeight);
	}
	else
	{
	 freqWeight = Image<RFLOAT>(sh,s);
	 freqWeight.data.initConstant(1.0);
	}


	if (do_tilt_fit)
	{
		if (precomp != "")
		{
			precomputed = true;
			ComplexIO::read(lastXY, precomp+"_xy", ".mrc");
			lastW.read(precomp+"_w.mrc");
			if (s != lastXY.data.ydim)
				REPORT_ERROR("ERROR: dimensions of precomputed tilt images are incompatible with those of the references!");
		}
		else
		{
			precomputed = false;
		}
	}

}

void CtfRefiner::fitDefocusOneMicrograph(long g, const std::vector<Image<Complex> > &obsF, const std::vector<Image<Complex> > &preds)
{

	long pc = obsF.size();

	std::stringstream stsg;
	stsg << g;

    if (!noGlobAstig)
    {
    	CTF ctf0;
        ctf0.read(mdts[g], mdts[g], 0);

        if (diag)
        {
            Image<RFLOAT> ctfFit(s,s);
            ctf0.getCenteredImage(ctfFit.data, angpix, false, false, false, false);
            if (debug)
            	VtkHelper::writeVTK(ctfFit, outPath+"_astig0_m"+stsg.str()+".vtk");
            else
            	ctfFit.write(outPath+"_astig0_m"+stsg.str()+".mrc");

            Image<RFLOAT> dotp0(sh,s), dotp0_full(s,s);
            dotp0.data.initZeros();

            for (long p = 0; p < pc; p++)
            {
                for (long y = 0; y < s; y++)
                for (long x = 0; x < sh; x++)
                {
                    Complex vx = DIRECT_A2D_ELEM(preds[p].data, y, x);
                    const Complex vy = DIRECT_A2D_ELEM(obsF[p].data, y, x);

                    dotp0(y,x) += vy.real*vx.real + vy.imag*vx.imag;
                }
            }

            FftwHelper::decenterDouble2D(dotp0.data, dotp0_full.data);
            if (debug)
            	VtkHelper::writeVTK(dotp0_full, outPath+"_astig_data_m"+stsg.str()+".vtk");
            else
            	dotp0_full.write(outPath+"_astig_data_m"+stsg.str()+".mrc");
        }

        double u, v, phi;
        DefocusRefinement::findAstigmatismNM(preds, obsF, freqWeight, ctf0, angpix, &u, &v, &phi);

        for (long p = 0; p < pc; p++)
        {
            mdts[g].setValue(EMDL_CTF_DEFOCUSU, u, p);
            mdts[g].setValue(EMDL_CTF_DEFOCUSV, v, p);
            mdts[g].setValue(EMDL_CTF_DEFOCUS_ANGLE, phi, p);
        }

        if (diag)
        {
            CTF ctf1;
            ctf1.read(mdts[g], mdts[g], 0);

            Image<RFLOAT> ctfFit(s,s);
            ctf1.getCenteredImage(ctfFit.data, angpix, false, false, false, false);
            if (debug)
            	VtkHelper::writeVTK(ctfFit, outPath+"_astig1_m"+stsg.str()+".vtk");
            else
            	ctfFit.write(outPath+"_astig1_m"+stsg.str()+".mrc");
        }
    }

    if (diag)
    {
        std::ofstream ofst(outPath+"_diag_m"+stsg.str()+".dat");
        std::ofstream ofsto(outPath+"_diag_m"+stsg.str()+"_opt.dat");

        for (long p = 0; p < pc; p++)
        {
            CTF ctf0;
            ctf0.read(mdts[g], mdts[g], p);

            std::vector<d2Vector> cost = DefocusRefinement::diagnoseDefocus(
                preds[p], obsF[p], freqWeight,
                ctf0, angpix, defocusRange, 100, nr_omp_threads);

            double cMin = cost[0][1];
            double dOpt = cost[0][0];

            for (int i = 0; i < cost.size(); i++)
            {
                ofst << cost[i][0] << " " << cost[i][1] << "\n";

                if (cost[i][1] < cMin)
                {
                    cMin = cost[i][1];
                    dOpt = cost[i][0];
                }
            }

            ofsto << dOpt << " " << cMin << "\n";

            ofst << "\n";
        }
    }
    else
    {
        #pragma omp parallel for num_threads(nr_omp_threads)
        for (long p = 0; p < pc; p++)
        {
            std::stringstream stsp;
            stsp << p;

            CTF ctf0;
            ctf0.read(mdts[g], mdts[g], p);

            CTF ctf(ctf0);

            if (fitAstigmatism)
            {
                double u, v, phi;
                DefocusRefinement::findAstigmatismNM(
                            preds[p], obsF[p], freqWeight, ctf0,
                            angpix, &u, &v, &phi);

                mdts[g].setValue(EMDL_CTF_DEFOCUSU, u, p);
                mdts[g].setValue(EMDL_CTF_DEFOCUSV, v, p);
                mdts[g].setValue(EMDL_CTF_DEFOCUS_ANGLE, phi, p);

                // Jasenko: why initialise ctf again?
                //ctf.DeltafU = u;
                //ctf.DeltafV = v;
                //ctf.azimuthal_angle = phi;
                //ctf.initialise();
            }
            else
            {
                double u, v;
                DefocusRefinement::findDefocus1D(
                            preds[p], obsF[p], freqWeight, ctf0,
                            angpix, &u, &v, defocusRange);

                mdts[g].setValue(EMDL_CTF_DEFOCUSU, u, p);
                mdts[g].setValue(EMDL_CTF_DEFOCUSV, v, p);

                // Jasenko: why initialise ctf again?
                //ctf.DeltafU = u;
                //ctf.DeltafV = v;
                //ctf.initialise();
            }
        }
    }

}

void CtfRefiner::fitBeamtiltOneMicrograph(long g, const std::vector<Image<Complex> > &obsF, const std::vector<Image<Complex> > &pred,
		 std::vector<Image<Complex> > &xyAcc, std::vector<Image<RFLOAT> > &wAcc)
{

	CTF ctf0;
    ctf0.read(mdts[g], mdts[g], 0);

    #pragma omp parallel for num_threads(nr_omp_threads)
	for (long p = 0; p < pred.size(); p++)
	{
		int threadnum = omp_get_thread_num();

		CTF ctf(ctf0);
		TiltRefinement::updateTiltShift(pred[p], obsF[p], ctf, angpix, xyAcc[threadnum], wAcc[threadnum]);
	}


}

void CtfRefiner::fitBeamTiltFromSumsAllMicrographs(Image<Complex> &xyAccSum, Image<RFLOAT> &wAccSum)
{

	if (verb > 0)
		std::cout << " Fitting beamtilt ..." << std::endl;

	Image<RFLOAT> wgh, phase, fit, phaseFull, fitFull;

    FilterHelper::getPhase(xyAccSum, phase);

    Image<Complex> xyNrm(sh,s);

    if (useFsc)
    {
        FilterHelper::multiply(wAccSum, freqWeight, wgh);
    }
    else
    {
        wgh = wAccSum;
    }

    Image<RFLOAT> imgdebug(sh,s);

    for (int y = 0; y < s; y++)
    for (int x = 0; x < sh; x++)
    {
        double xx = x;
        double yy = y <= sh? y : y - s;
        double r = sqrt(xx*xx + yy*yy);

        if (r == 0 || sh/(2.0*angpix*r) > kmin)
        {
            wgh(y,x) = 0.0;
        }

        imgdebug(y,x) = r == 0? 0.0 : sh/(2.0*angpix*r) - kmin;

        xyNrm(y,x) = wAccSum(y,x) > 0.0? xyAccSum(y,x)/wAccSum(y,x) : Complex(0.0, 0.0);
    }

    if (debug)
    	VtkHelper::writeVTK(imgdebug, outPath+"_debug.vtk");

    Image<RFLOAT> wghFull;
    FftwHelper::decenterDouble2D(wgh(), wghFull());
    if (debug)
    {
    	VtkHelper::writeVTK(wghFull, outPath+"_weight.vtk");
    }
    else
    {
    	wghFull.write(outPath+"_weight.mrc");
    }

    FftwHelper::decenterUnflip2D(phase.data, phaseFull.data);

    if (debug)
    {
    	VtkHelper::writeVTK(phaseFull, outPath+"_delta_phase.vtk");
    }
    else
    {
		phaseFull.write(outPath+"_delta_phase.mrc");
		wgh.write(outPath+"_weight.mrc");
    }

    RFLOAT shift_x, shift_y, tilt_x, tilt_y;

    TiltRefinement::fitTiltShift(phase, wgh, Cs, lambda, angpix,
                                 &shift_x, &shift_y, &tilt_x, &tilt_y, &fit);



    FftwHelper::decenterUnflip2D(fit.data, fitFull.data);
    if (debug)
    {
    	VtkHelper::writeVTK(fitFull, outPath+"_delta_phase_fit.vtk");
    }
    else
    {
    	fitFull.write(outPath+"_delta_phase_fit.mrc");
    }

    // Write beamtilt to a text file???!
    // TODO: write into the particle STAR file!
    std::ofstream os(outPath+"_beam_tilt_0.txt");
    os << "beamtilt_x = " << tilt_x << "\n";
    os << "beamtilt_y = " << tilt_y << "\n";
    os.close();

    double tilt_xx, tilt_xy, tilt_yy;

    if (aniso)
    {
        TiltRefinement::optimizeAnisoTilt(
            xyNrm, wgh, Cs, lambda, angpix, false,
            shift_x, shift_y, tilt_x, tilt_y,
            &shift_x, &shift_y, &tilt_x, &tilt_y,
            &tilt_xx, &tilt_xy, &tilt_yy, &fit);
    }
    else
    {
        TiltRefinement::optimizeTilt(
            xyNrm, wgh, Cs, lambda, angpix, false,
            shift_x, shift_y, tilt_x, tilt_y,
            &shift_x, &shift_y, &tilt_x, &tilt_y, &fit);
    }


    FftwHelper::decenterUnflip2D(fit.data, fitFull.data);
    if (debug)
    {
    	VtkHelper::writeVTK(fitFull, outPath+"_delta_phase_iter_fit.vtk");
    }
    else
    {
    	fitFull.write(outPath+"_delta_phase_iter_fit.mrc");
    }

    // Write beamtilt to a text file???!
    // TODO: write into the particle STAR file!
    std::ofstream os2(outPath+"_beam_tilt_1.txt");
    os2 << "beamtilt_x = " << tilt_x << "\n";
    os2 << "beamtilt_y = " << tilt_y << "\n";
    os2.close();


}

void CtfRefiner::processSubsetMicrographs(long g_start, long g_end, Image<Complex> &xyAccSum, Image<RFLOAT> &wAccSum)
{

	std::vector<Image<Complex>> xyAcc(nr_omp_threads);
    std::vector<Image<RFLOAT>> wAcc(nr_omp_threads);
    if (do_tilt_fit && !precomputed)
    {
		for (int i = 0; i < nr_omp_threads; i++)
		{
			xyAcc[i] = Image<Complex>(sh,s);
			xyAcc[i].data.initZeros();

			wAcc[i] = Image<RFLOAT>(sh,s);
			wAcc[i].data.initZeros();
		}
    }

    int barstep;
	int my_nr_micrographs = g_end - g_start + 1;
    if (verb > 0)
	{
		std::cout << " + Performing loop over all micrographs ... " << std::endl;
		init_progress_bar(my_nr_micrographs);
		barstep = XMIPP_MAX(1, my_nr_micrographs/ 60);
	}

    std::vector<ParFourierTransformer> fts(nr_omp_threads);

	long nr_done = 0;
	for (long g = g_start; g <= g_end; g++)
	{

		std::vector<Image<Complex> > obsF;

		// both defocus_tit and tilt_fit need the same obsF
		obsF = StackHelper::loadStackFS(&mdts[g], imgPath, nr_omp_threads, &fts);
		const long pc = obsF.size();

		std::vector<Image<Complex>> preds(pc);

		// defocus_fit needs code as below. tilt_fit used StackHelper::projectStackPar, but results are the same
		// TODO: to be confirmed by Jasenko!
		#pragma omp parallel for num_threads(nr_omp_threads)
		for (long p = 0; p < pc; p++)
		{
			int randSubset;
			mdts[g].getValue(EMDL_PARTICLE_RANDOM_SUBSET, randSubset, p);
			randSubset -= 1;

			preds[p] = obsModel.predictObservation(
				projectors[randSubset], mdts[g], p, false, true);
		}

		if (do_defocus_fit)
			fitDefocusOneMicrograph(g, obsF, preds);

		if (do_tilt_fit)
			fitBeamtiltOneMicrograph(g, obsF, preds, xyAcc, wAcc);

		//if (do_cs_fit)
		//	fitCsOneMicrograph();

		nr_done++;
		if (verb > 0 && nr_done % barstep == 0)
			progress_bar(nr_done);

	}

	// Combine the accumulated weights from all threads for this subset, store weighted sums in xyAccSum and wAccSum
	for (int i = 0; i < nr_omp_threads; i++)
	{
		ImageOp::linearCombination(xyAccSum, xyAcc[i], 1.0, 1.0, xyAccSum);
		ImageOp::linearCombination(wAccSum, wAcc[i], 1.0, 1.0, wAccSum);
	}

    if (verb > 0)
	{
		progress_bar(my_nr_micrographs);
	}



}


void CtfRefiner::run()
{


    // If there were multiple groups of micrographs, we could consider introducing a loop over those here...
    //for (int igroup = 0; igroup < nr_micrographs_groups; igroup++)
    //{

    Image<Complex> xyAccSum;
    Image<RFLOAT> wAccSum;
	if (do_tilt_fit && !precomputed)
    {
    	xyAccSum().initZeros(sh,s);
    	wAccSum().initZeros(sh,s);
    }
	else
	{
		xyAccSum = lastXY;
		wAccSum = lastW;
	}

    if (do_defocus_fit || (do_tilt_fit && !precomputed) )
    {
    	// The subsets will be used in openMPI parallelisation: instead of over g0->gc, they will be over smaller subsets
    	processSubsetMicrographs(g0, gc, xyAccSum, wAccSum);
    }

	if (do_tilt_fit)
		fitBeamTiltFromSumsAllMicrographs(xyAccSum, wAccSum);

	//} // end loop over igroup

	// TODO: design mechanism to set the defocus parameters of all individual particles from different MPI ranks...
	// TODO: will need to pass the values through MPI_Send....
    MetaDataTable mdtAll;
    mdtAll.reserve(mdt0.numberOfObjects());
	for (long g = g0; g <= gc; g++)
		mdtAll.append(mdts[g]);

	mdtAll.write(outPath + "particles.star");

}



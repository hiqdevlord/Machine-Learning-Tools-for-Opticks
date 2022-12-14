/*
* The information in this file is
* Copyright(c) 2012, Himanshu Singh <91.himanshu@gmail.com>
* and is subject to the terms and conditions of the
* GNU Lesser General Public License Version 2.1
* The license text is available from   
* http://www.gnu.org/licenses/lgpl.html
*/

#include "AoiElement.h"
#include "AoiLayer.h"
#include "AppVerify.h"
#include "BitMaskIterator.h"
#include "DataAccessor.h"
#include "DataAccessorImpl.h"
#include "DataElementGroup.h"
#include "DataRequest.h"
#include "DesktopServices.h"
#include "LayerList.h"
#include "Location.h"
#include "ModelServices.h"
#include "ObjectResource.h"
#include "PlugInArg.h"
#include "PlugInArgList.h"
#include "PlugInManagerServices.h"
#include "PlugInRegistration.h"
#include "PlugInResource.h"
#include "ProgressTracker.h"
#include "PseudocolorLayer.h"
#include "RasterDataDescriptor.h"
#include "RasterUtilities.h"
#include "Signature.h"
#include "SignatureSet.h"
#include "SignatureSelector.h"
#include "SpatialDataView.h"
#include "SpectralUtilities.h"
#include "ML_Tools_Version.h"
#include "ISODATA.h"
#include "ISODATADlg.h"

#include <QtCore/QTime>
#include <QtCore/QString>
#include <QtGui/QInputDialog>
#include <QtGui/QMessageBox>

#include <limits>
#include <string>
#include <vector>
#include <algorithm>

REGISTER_PLUGIN_BASIC(SpectralISODATA, ISODATA);

namespace
{
	/**
	* Calculated euclidean distance between cluster centre and given point.
	*
	* @param pixel
	*        The pixel that is part of the cluster whose centroid is @centre.
	* @param centre
	*        The centre of the cluster from which distances are calculated.
	* @param distance
	*        The results are stored in this variable;
	* @return void.
	*
	*/
	template<typename T>
	void pixelDistance(T* pixel, std::vector<double>& centre, double &distance)
	{
		for (std::vector<double>::size_type band = 0; band < centre.size(); ++band)
		{
			distance += (centre[band] - pixel[band])*(centre[band] - pixel[band]);
		}
		distance = sqrt(distance);

	}

	double pixelDistance(std::vector<double>& A, std::vector<double>& B)
	{
		double distance = 0.0;
		for (std::vector<double>::size_type band = 0; band < B.size(); ++band)
		{
			distance += (A[band] - B[band])*(A[band] - B[band]);
		}
		return distance;
	}

	double pixelDistance(Signature* A, Signature* B)
	{
		double distance = 0.0;
		std::vector<double> Ad;
		std::vector<double> Bd;
		DataVariant reflectance;
		reflectance = A->getData("Reflectance");
		reflectance.getValue(Ad);
		reflectance = B->getData("Reflectance");
		reflectance.getValue(Bd);
		for (std::vector<double>::size_type band = 0; band < Bd.size(); ++band)
		{
			distance += (Ad[band] - Bd[band])*(Ad[band] - Bd[band]);
		}
		distance = sqrt(distance);
		return distance;
	}

	/**
	* Used to calculated variance between cluster centre and the point in the cluster.
	* This function is called for every point in the cluster
	*
	* @param pixel
	*        The pixel that is part of the cluster whose centroid is @centre.
	* @param centre
	*        The centre of the cluster from which distances are calculated.
	* @param variance
	*        This parameter is updated everytime the function is called with variance.
	* @param numPoints
	*        Number of points that are present in the cluster.
	* @return void.
	*
	*/
	template<typename T>
	void calculateVariance(T* pixel, std::vector<double>& centre, std::vector<double>& variance, int &numPoints)
	{
		for (std::vector<double>::size_type band = 0; band < centre.size(); ++band)
		{
			variance[band] += (centre[band] - pixel[band])*(centre[band] - pixel[band])/numPoints;
		}

	}

	/**
	* Calculates the Maximum Standard Deviation and its index from the given variance.
	*
	* @param variance
	*        The variance of cluster points from centre.
	*
	* @return pair containing the maximum Standard Deviation and its index.
	*
	*/
	std::pair<double, int> calculateMaxSTDVfromVariance (std::vector<double>& variance)
	{
		double MaxSTDV = sqrt(variance[0]);
		int IndexofMaxSTDV = 0;
		for (std::vector<double>::size_type band = 1; band < variance.size(); ++band)
		{
			double stdv = sqrt(variance[band]);
			if (MaxSTDV < stdv)
			{
				MaxSTDV = stdv;
				IndexofMaxSTDV = band;
			}
		}
		return std::make_pair(MaxSTDV, IndexofMaxSTDV);
	}

	// Used when intercluster distances are calculated
	struct pairDist_t
	{
		double dist;
		int a, b;
		pairDist_t(double _dist, int _a, int _b) : dist(_dist), a(_a), b(_b)
		{}
		bool operator<(pairDist_t toCompare)  const                  
		{
			return  (dist < toCompare.dist);

		}
		bool operator<(double toCompare) const
		{
			return (dist < toCompare);
		} 

	};

};
ISODATA::ISODATA()
{
	setName("ISODATA");
	setDescription("ISODATA Spectral Clustering Algorithm");
	setDescriptorId("{0E3E9D75-57C4-4FAE-BDBD-33C79C7FCB97}");
	setCopyright(ML_TOOLS_COPYRIGHT);
	setVersion(ML_TOOLS_VERSION_NUMBER);
	setProductionStatus(ML_TOOLS_IS_PRODUCTION_RELEASE);
	setAbortSupported(true);
	setMenuLocation("[ML Tools]/ISODATA");
}

ISODATA::~ISODATA()
{}

bool ISODATA::getInputSpecification(PlugInArgList*& pInArgList)
{
	VERIFY(pInArgList = Service<PlugInManagerServices>()->getPlugInArgList());
	VERIFY(pInArgList->addArg<Progress>(ProgressArg(), NULL));
	VERIFY(pInArgList->addArg<SpatialDataView>(ViewArg()));
	VERIFY(pInArgList->addArg<double>("SAMThreshold", static_cast<double>(85.0),
		"SAM Threshold. Default is 85.0."));
	VERIFY(pInArgList->addArg<double>("Maximum STDV", static_cast<double>(0.0),
		"Maximum Standard Deviation of points from their cluster centers along each axis."));
	VERIFY(pInArgList->addArg<double>("Minimum Centre Distance", static_cast<double>(0.0),
		"Minimum required distance between two cluster centers."));
	VERIFY(pInArgList->addArg<unsigned int>("Maximum Iterations", static_cast<unsigned int>(10),
		"Maximum number of iterations for which the algorithm will run."));
	VERIFY(pInArgList->addArg<unsigned int>("Initial Clusters", static_cast<unsigned int>(2),   
		"The number of intial Clusters that will be used to run the algorithm."));
	VERIFY(pInArgList->addArg<int>("Minimum Cluster Points", static_cast<int>(10),
		"Minimum number of points that can form a Cluster."));
	VERIFY(pInArgList->addArg<unsigned int>("Maximum Merge Pairs", static_cast<unsigned int>(0),
		"Minimum number of points that can form a Cluster."));
	VERIFY(pInArgList->addArg<std::string>("Results Name", "ISODATA Results",
		"Determines the name for the results of the clustering."));

	return true;
}

bool ISODATA::getOutputSpecification(PlugInArgList*& pOutArgList)
{
	VERIFY(pOutArgList = Service<PlugInManagerServices>()->getPlugInArgList());
	VERIFY(pOutArgList->addArg<DataElementGroup>("ISODATA Result", NULL,
		"Data element group containing all results from the clustering as well as the centroids used."));
	VERIFY(pOutArgList->addArg<RasterElement>("ISODATA Results Element", NULL,
		"Raster element resulting from the Clustering."));
	VERIFY(pOutArgList->addArg<PseudocolorLayer>("ISODATA Results Layer", NULL,
		"Pseudocolor layer resulting from the clustering."));
	return true;
}

bool ISODATA::getInputArguments(PlugInArgList* pInArgList)
{
	if (pInArgList == NULL)
		return false;

	progress = ProgressTracker(pInArgList->getPlugInArgValue<Progress>(ProgressArg()),
		"Executing ISODATA", "spectral", "{2925A495-54FD-4E3B-A92A-3D5891A0D277}");

	// Application batch mode is not supported because the output requires a pseudocolor layer.
	if (Service<ApplicationServices>()->isBatch() == true)
	{
		progress.report("ISODATA does not support application batch mode.", 0, ERRORS, true);
		return false;
	}

	pView = pInArgList->getPlugInArgValue<SpatialDataView>(ViewArg());
	if (pView == NULL)
	{
		progress.report("Invalid view.", 0, ERRORS, true);
		return false;
	}

	VERIFY(pInArgList->getPlugInArgValue("SAMThreshold", SAMThreshold) == true);
	if (SAMThreshold <= 0.0)
	{
		progress.report("Invalid SAM threshold.", 0, ERRORS, true);
		return false;
	}

	VERIFY(pInArgList->getPlugInArgValue("Maximum Iterations", MaxIterations) == true);

	VERIFY(pInArgList->getPlugInArgValue("Initial Clusters", NumClus) == true);
	if (NumClus <= 0)
	{
		progress.report("Invalid initial clusters.", 0, ERRORS, true);
		return false;
	}

	VERIFY(pInArgList->getPlugInArgValue("Maximum STDV", MaxSTDV) == true);
	if (MaxSTDV < 0.0)
	{
		progress.report("Invalid Maximum STDV.", 0, ERRORS, true);
		return false;
	}

	VERIFY(pInArgList->getPlugInArgValue("Minimum Centre Distance", Lump) == true);
	if (Lump < 0.0)
	{
		progress.report("Invalid Minimum Center Distance.", 0, ERRORS, true);
		return false;
	}

	VERIFY(pInArgList->getPlugInArgValue("Minimum Cluster Points", SamPrm) == true);

	VERIFY(pInArgList->getPlugInArgValue("Maximum Merge Pairs", MaxPair) == true);

	VERIFY(pInArgList->getPlugInArgValue("Results Name", resultsName) == true);

	// Show interavtive dialog if  the application is not running in Batch Mode.
	if (isBatch() == false)
	{
		ISODATADlg ISODATADlg(SAMThreshold, MaxIterations, NumClus,
			Lump, MaxSTDV, SamPrm, MaxPair, Service<DesktopServices>()->getMainWidget());

		if (ISODATADlg.exec() != QDialog::Accepted)
		{
			progress.report("Unable to obtain input parameters.", 0, ABORT, true);
			return false;
		}
		SAMThreshold = ISODATADlg.getSAMThreshold();
		MaxIterations = ISODATADlg.getMaxIterations();
		NumClus = ISODATADlg.getNumClus();
		Lump = ISODATADlg.getLump();
		MaxSTDV = ISODATADlg.getMaxSTDV();
		SamPrm = ISODATADlg.getSamPrm();
		MaxPair = ISODATADlg.getMaxPair();
	}
	return true;
}

bool ISODATA::execute(PlugInArgList* pInArgList, PlugInArgList* pOutArgList)
{
	//Extract Input Arguments
	if (getInputArguments(pInArgList) == false)
	{
		progress.report("Unable to obtain input arguments", 0, ERRORS, true);
		return false;
	}
	LayerList* pLayerList = pView->getLayerList();
	VERIFY(pLayerList != NULL);

	RasterElement* pRasterElement = pLayerList->getPrimaryRasterElement();
	if (pRasterElement == NULL)
	{
		progress.report("Invalid raster element.", 0, ERRORS, true);
		return false;
	}

	RasterDataDescriptor* pDescriptor = dynamic_cast<RasterDataDescriptor*>(pRasterElement->getDataDescriptor());
	if (pDescriptor == NULL)
	{
		progress.report("Invalid raster data descriptor.", 0, ERRORS, true);
		return false;
	}
	//Select the initial NumClus centroids randomly.
	//The centroids are signatures and spectral distance is used to cluster them.
	for (unsigned int i = 0; i < NumClus; ++i)
	{
		Opticks::PixelLocation location(rand() % pDescriptor->getColumnCount(), rand() % pDescriptor->getRowCount());
		Signature* pSignature = SpectralUtilities::getPixelSignature(pRasterElement, location);

		if (pSignature == NULL)
		{
			progress.report("Failed to get pixel signature.", 0, ERRORS, true);
			return false;
		}
		centroids.push_back(pSignature);
	}
	//Load the SAM plugin, SAM is used to cluster points using spectral distance
	ExecutableResource pSam("SAM", std::string(), progress.getCurrentProgress());
	if (pSam.get() == NULL)
	{
		progress.report("SAM is not available.", 0, ERRORS, true);
		return false;
	}

	//Delete Previous results if any and create new result element
	ModelResource<DataElementGroup> pResultElement(dynamic_cast<DataElementGroup*>(Service<ModelServices>()->getElement(
		resultsName, TypeConverter::toString<DataElementGroup>(), pRasterElement)));
	pResultElement = ModelResource<DataElementGroup>(reinterpret_cast<DataElementGroup*>(NULL));
	pResultElement = ModelResource<DataElementGroup>(dynamic_cast<DataElementGroup*>(
		Service<ModelServices>()->createElement(resultsName,
		TypeConverter::toString<DataElementGroup>(), pRasterElement)));
	if (pResultElement.get() == NULL)
	{
		progress.report("Unable to create result element.", 0, ERRORS, true);
		return false;
	}

	//Centroids for first iteration.
	ModelResource<SignatureSet> pSignatureSet(dynamic_cast<SignatureSet*>(Service<ModelServices>()->createElement(
		"Centroids for Iteration 1", TypeConverter::toString<SignatureSet>(), pResultElement.get())));
	if (pSignatureSet.get() == NULL)
	{
		progress.report("Unable to create signature set.", 0, ERRORS, true);
		return false;
	}

	//The number of clusters (Set to Initial number of clusters).
	unsigned int clusters = NumClus;

	// Begin iterations
	for (unsigned int iterationNumber = 1; iterationNumber <= MaxIterations; ++iterationNumber)
	{
		if (isAborted() == true)
		{
			progress.report("User Aborted.", 0, ABORT, true);
			return false;
		}
		if (clusters <= 1)
		{
			progress.report("Invalid argument values supplied", 0, ABORT, true);
		}

		// Insert the centroids into the signature set.
		if (pSignatureSet->insertSignatures(centroids) == false)
		{
			progress.report("Unable to add centroids to signature set.", 0, ERRORS, true);
			return false;
		}

		PseudocolorLayer* pSamLayer = runSamOnCentroids(pSam, pSignatureSet.get(), iterationNumber, pRasterElement);
		if (pSamLayer == NULL)
		{
			return false;
		}
		Service<ModelServices>()->setElementParent(pSamLayer->getDataElement(), pResultElement.get());

		// Force a new signature set to be created.
		ModelResource<SignatureSet> pNewSignatureSet(dynamic_cast<SignatureSet*>(
			Service<ModelServices>()->createElement(
			QString("Centroids for Iteration %1").arg(iterationNumber + 1).toStdString(),
			TypeConverter::toString<SignatureSet>(), pResultElement.get())));
		if (pNewSignatureSet.get() == NULL)
		{
			progress.report("Unable to create new signature set.", 0, ERRORS, true);
			return false;
		}

		//Clear the old centroids.
		centroids.clear();

		//Class IDs generated by SAM
		std::vector <int> classIds;
		pSamLayer->getClassIDs(classIds);

		// Hide all classes.
		for (std::vector<int>::const_iterator iter = classIds.begin(); iter != classIds.end(); ++iter)
		{
			pSamLayer->setClassDisplayed(*iter, false);
		}

		// Indicates if rest of the iteration is to be skipped.
		int repeat = 0;

		// Average distances of points from thier centroids.
		std::vector<double> average;
		double totalAvg = 0.0;

		// Number of points in a cluster
		std::vector <int> numPoints;
		int totalPoints = 0;

		// Maximum Standard deviation for each centroid(pair <MaxSTDV, IndexofMaxSTDV>)
		// Index is requied to calculate new centroids if the original cluster is split.
		std::vector<std::pair<double, int> > maxCentroidSTDV;

		// Show a class and obtain AOI from it.
		// Use the obtained AOI to find the number of points in a cluster and
		// generate new centroids from clusters having >= SamPrm.
		for (unsigned int i = 0; i < classIds.size(); ++i)
		{
			// Display the class so that it will be included in the derived AOI.
			pSamLayer->setClassDisplayed(classIds[i], true);

			// Derive an AOI from the pseudocolor layer.
			AoiLayer* pAoiLayer = dynamic_cast<AoiLayer*>(pView->deriveLayer(pSamLayer, AOI_LAYER));
			if (pAoiLayer == NULL)
			{
				progress.report("Failed to derive AOI from pseudocolor layer.", 0, ERRORS, true);
				return false;
			}

			// Use a ModelResource so pAoiLayer gets deleted when pAoiElement goes out of scope.
			ModelResource<AoiElement> pAoiElement(dynamic_cast<AoiElement*>(pAoiLayer->getDataElement()));
			if (pAoiElement.get() == NULL)
			{
				progress.report("Failed to obtain AOI element from layer.", 0, ERRORS, true);
				return false;
			}

			// Obtain iterator over the AOI layer
			BitMaskIterator iterator(pAoiElement->getSelectedPoints(), pRasterElement);
			// Check for empty AOI -- an empty AOI implies that  "Indeterminate" or "No Match" results were found.
			if (iterator != iterator.end())
			{
				// If the number of points are less than the minimum required
				if (iterator.getCount() < SamPrm)
				{
					repeat = 1;
					// Decrement the number of clusters
					clusters--;
					//Ignore this cluster i.e. Don't add this to the list of new centroids.
					//Hide this class
					pSamLayer->setClassDisplayed(classIds[i], false);
					continue;
				}
				// Compute the centroid for the class.
				// These signatures will be used next iteration.
				ModelResource<Signature> pSignature(dynamic_cast<Signature*>(Service<ModelServices>()->createElement(
					QString("ISODATA Iteration %1: Centroid %2").arg(iterationNumber + 1).arg(centroids.size() + 1).toStdString(),
					TypeConverter::toString<Signature>(), pNewSignatureSet.get())));
				if (pSignature.get() == NULL)
				{
					progress.report("Failed to create new signature for centroid.", 0, ERRORS, true);
					return false;
				}

				if (SpectralUtilities::convertAoiToSignature(pAoiElement.get(), pSignature.get(),
					pRasterElement, progress.getCurrentProgress(), &mAborted) == false)
				{
					progress.report("Failed to derive AOI from pseudocolor layer.", 0, ERRORS, true);
					return false;
				}
				centroids.push_back(pSignature.release());

				// Compute the Average distances of pixels from the centroid.
				// Access the reflectence of centroid and for each pixel caluclate the distance from it.
				// Accumulate  for all pixel in the cluster and divide by the numbe of pixels
				// Note: All the calculations are performed by assuming each point to be a bandCount() dimensional vector
				//       and finding the euclidean distance.

				// Acquire centroid's reflectence values
				Signature* pCentroidSignature = centroids.back();
				std::vector <double> centroidValue;
				DataVariant reflectanceVariant = pCentroidSignature->getData("Reflectance");
				reflectanceVariant.getValue(centroidValue);

				int startRow = iterator.getBoundingBoxStartRow();
				int endRow = iterator.getBoundingBoxEndRow();
				int startCol = iterator.getBoundingBoxStartColumn();
				int endCol = iterator.getBoundingBoxEndColumn();

				FactoryResource<DataRequest> request;
				request->setInterleaveFormat(BIP);
				request->setRows(pDescriptor->getActiveRow(startRow),
					pDescriptor->getActiveRow(endRow));
				request->setColumns(pDescriptor->getActiveColumn(startCol),
					pDescriptor->getActiveColumn(endCol));
				DataAccessor accessor = pRasterElement->getDataAccessor(request.release());
				VERIFY(accessor.isValid());

				// Number of points in the cluster
				numPoints.push_back(iterator.getCount());
				totalPoints += numPoints.back();

				//variance for this centroid
				std::vector<double> variance(pDescriptor->getBandCount(), 0.0);

				// Sum of distances from cluster points to the centroid
				double sumDist = 0.0;
				for (int row = startRow; row <= endRow; row++) 
				{
					if (isAborted() == true)
					{
						progress.report("User Aborted.", 0, ABORT, true);
						return false;
					}
					progress.report(QString("Calculating Average Distance and Maximum STDV for Centroid %1").arg(centroids.size()).toStdString(),
						(100*row)/(endRow-startRow+1), NORMAL, true);

					for (int col = startCol; col <= endCol; col++) 
					{
						//If the pixel is present in cluster
						if (iterator.getPixel(col, row)) 
						{
							accessor->toPixel(row, col);
							VERIFY(accessor.isValid());
							double distance = 0.0;
							switchOnEncoding(pDescriptor->getDataType(), pixelDistance, accessor->getColumn(), centroidValue, distance);
							sumDist += distance;

							switchOnEncoding(pDescriptor->getDataType(), calculateVariance, accessor->getColumn(), centroidValue,
								variance, numPoints.back());
						}
					}
				}
				totalAvg += sumDist;
				double avg = sumDist/numPoints.back();
				average.push_back(avg);

				maxCentroidSTDV.push_back(calculateMaxSTDVfromVariance(variance));
			}
			//Hide this class
			pSamLayer->setClassDisplayed(classIds[i], false);
		}

		// Overall average of distances from cluster centres
		totalAvg = totalAvg/totalPoints;

		// Show all classes.
		for (std::vector<int>::const_iterator iter = classIds.begin(); iter != classIds.end(); ++iter)
		{
			pSamLayer->setClassDisplayed(*iter, true);
		}

		// If there were clusters with < SamPrm points then start new iteration.
		if (repeat)
		{
			// Hide previous layer.
			pView->hideLayer(pSamLayer);
			pSignatureSet.release();
			pSignatureSet = ModelResource<SignatureSet>(pNewSignatureSet.release());
			continue;
		}

		// Execute steps 5-8 if the condition below is true.
		if ((iterationNumber != MaxIterations) && (2*clusters <= NumClus || iterationNumber%2) && (clusters < 2*NumClus))
		{
			// Index of centroids of clusters that are going to be split.
			std::vector<int> toSplit;
			// Check and centroids that will be split.
			for (unsigned int c = 0; c < centroids.size(); c++)
			{
				if ((maxCentroidSTDV[c].first > MaxSTDV) && ((average[c] > totalAvg && numPoints[c] > 2*(SamPrm + 1))
					|| (clusters <= NumClus/2)))
				{
					toSplit.push_back(c);
				}
			}

			// Split the centroids.
			// The original centroid is split into two. Denote the index with maximumSTDV by idx.
			// changeFactor = 0.5*MaxSTDV; Then the two new centroids have the same value as OriginalReflectance but for:
			//  1.) OrginalReflectance[idx] = OriginalReflectance[idx] - changeFactor
			//  2.) OrginalReflectance[idx] = OriginalReflectance[idx] + changeFactor
			for (unsigned int s = 0; s < toSplit.size(); s++)
			{
				int cindex = toSplit[s];

				std::vector<double> originalReflectance;
				DataVariant reflectanceVariant = centroids[cindex]->getData("Reflectance");
				reflectanceVariant.getValue(originalReflectance);
				// For the first centroid
				double changeFactor = 0.5*maxCentroidSTDV[cindex].first;

				originalReflectance[maxCentroidSTDV[cindex].second] -= changeFactor;

				centroids[cindex]->setData("Reflectance", originalReflectance);

				// Obtain new signature for the second centroid
				ModelResource<Signature> pSignature(dynamic_cast<Signature*>(Service<ModelServices>()->createElement(
					QString("ISODATA Iteration %1: Centroid %2 (split from Centroid %3)").arg(iterationNumber + 1).arg(centroids.size() + 1).arg(cindex + 1).toStdString(),
					TypeConverter::toString<Signature>(), pNewSignatureSet.get())));
				if (pSignature.get() == NULL)
				{
					progress.report("Failed to create new signature for centroid.", 0, ERRORS, true);
					return false;
				}
				originalReflectance[maxCentroidSTDV[cindex].second] += 2*changeFactor;
				pSignature.get()->setData("Reflectance", originalReflectance);
				pSignature.get()->setData("Wavelength", centroids[cindex]->getData("Wavelength"));
				pSignature.get()->setData("BandNumber", centroids[cindex]->getData("BandNumber"));
				centroids.push_back(pSignature.release());
				// Increase cluster count
				clusters++;
			}

			// If some cluster was split then repeat the iteration
			if (toSplit.size() != 0) 
			{
				repeat = 1;
			}
		}
		// If the cluster was split
		if (repeat)
		{
			pView->hideLayer(pSamLayer);
			pSignatureSet.release();
			pSignatureSet = ModelResource<SignatureSet>(pNewSignatureSet.release());
			continue;
		}

		// Perform LUMP i.e. Merge those clusters whose inter-cluster distance is < Lump
		// InterCluster distances are calculated and sorted in ascending order.
		// A maximum of MaxPair can be merged per iteration

		// If this is the final iteration set lump to zero.
		if (iterationNumber == MaxIterations) Lump = 0;
		std::vector<pairDist_t> interClus;
		for (unsigned int a = 0; a < centroids.size(); a++)
		{
			progress.report("Computing inter-cluster distances", ((a+1)*100)/centroids.size(), NORMAL, true);
			for (unsigned int b = a + 1; b < centroids.size(); b++)
			{
				interClus.push_back(pairDist_t(pixelDistance(centroids[a], centroids[b]), a, b));
			}
		}

		// Sort the distances in ascending order
		std::sort(interClus.begin(), interClus.end());
		// True if a centoid is involved in a merger before
		std::vector<bool> inMerge(centroids.size(), false);
		unsigned int m, n;
		// Take at most MaxPair
		for (m = 0, n = 1; m < interClus.size() && n <= MaxPair; m++, n++)
		{
			progress.report("Checking for pairs to merge", (n*100)/std::min(MaxPair,interClus.size()), NORMAL, true);
			int c1 = interClus[m].a, c2 = interClus[m].b;
			// If the distance is less than Lump and the clusters were not involved in mergers before then merge clusters
			if (interClus[m].dist < Lump && !inMerge[c1] && !inMerge[c2])
			{
				inMerge[c1] = true; inMerge[c2] = true;
				// Obtain new signature for the centroid
				ModelResource<Signature> pSignature(dynamic_cast<Signature*>(Service<ModelServices>()->createElement(
					QString("ISODATA Iteration %1: Centroid from merging %2 and %3").arg(iterationNumber + 1).arg(c1 + 1).arg(c2 + 1).toStdString(),
					TypeConverter::toString<Signature>(), pNewSignatureSet.get())));
				if (pSignature.get() == NULL)
				{
					progress.report("Failed to create new signature for centroid.", 0, ERRORS, true);
					return false;
				}

				// Reflectance of new centroid will be (Na*Za + Nb*Zb)/(Na + Nb) where Ni is number of point in cluster and Zi is reflectance.
				std::vector<double> Za, Zb;
				int Na = numPoints[c1], Nb = numPoints[c2];
				DataVariant reflectanceVariant = centroids[c1]->getData("Reflectance");
				reflectanceVariant.getValue(Za);
				reflectanceVariant = centroids[c2]->getData("Reflectance");
				reflectanceVariant.getValue(Zb);

				// Result
				std::vector<double> result(Za.size());
				for (unsigned int z = 0; z < Za.size(); z++)
				{
					result[z] = (Na*Za[z] + Nb*Zb[z])/(Na + Nb);
				}
				pSignature->setData("Reflectance", result);
				pSignature->setData("Wavelength", centroids[c1]->getData("Wavelength"));
				pSignature->setData("BandNumber", centroids[c1]->getData("BandNumber"));
				// Insert the merged centroid
				centroids.push_back(pSignature.release());
				// Decrease cluster count
				clusters--;
			}
		}
		// Erase those points which were merged in the last steps i.e inMerge
		for (int m = inMerge.size() - 1; m >= 0; m--)
		{
			if (inMerge[m] == true)
			{
				centroids.erase(centroids.begin() + m);
			}
		}

		if (iterationNumber != MaxIterations) 
		{
			pView->hideLayer(pSamLayer);
			pSignatureSet.release();
			pSignatureSet = ModelResource<SignatureSet>(pNewSignatureSet.release());
		}
		else
		{
			pResultElement.release();
			pSignatureSet.release();

			// Rename the final result layer and its data element.
			pSamLayer->rename(resultsName + " Layer");
			Service<ModelServices>()->setElementName(pSamLayer->getDataElement(), resultsName + " Element");
			Service<ModelServices>()->setElementName(pSignatureSet.get(), resultsName + " Centroids");
			// Set output arguments.
			if (pOutArgList != NULL)
			{
				pOutArgList->setPlugInArgValue<DataElementGroup>("ISODATA Result",
					dynamic_cast<DataElementGroup*>(pResultElement.get()));
				pOutArgList->setPlugInArgValue<RasterElement>("ISODATA Results Element",
					dynamic_cast<RasterElement*>(pSamLayer->getDataElement()));
				pOutArgList->setPlugInArgValue<PseudocolorLayer>("ISODATA Results Layer", pSamLayer);
			}
		}
	}

	progress.report("ISODATA complete", 100, NORMAL);
	progress.upALevel();
	return true;
}

PseudocolorLayer* ISODATA::runSamOnCentroids(ExecutableResource& pSam, SignatureSet* target, int iterationNumber, RasterElement* pRasterElement)
{
	// Call SAM on the centroids.
		PlugInArgList& samInput = pSam->getInArgList();
		std::string samResultsName = resultsName + QString(" for Iteration %1").arg(iterationNumber).toStdString();
		bool samSuccess = samInput.setPlugInArgValue<Signature>("Target Signatures", target);
		samSuccess &= samInput.setPlugInArgValue<RasterElement>(DataElementArg(), pRasterElement);
		samSuccess &= samInput.setPlugInArgValue<std::string>("Results Name", &samResultsName);
		samSuccess &= samInput.setPlugInArgValue<double>("Threshold", &SAMThreshold);
		samSuccess &= samInput.setPlugInArgValue<bool>("Display Results", &samSuccess);
		samSuccess &= pSam->execute();
		if (samSuccess == false)
		{
			progress.report("SAM failed to execute.", 0, ERRORS, true);
			return NULL;
		}
		// Extract SAM results
		PlugInArgList& samOutput = pSam->getOutArgList();
		RasterElement* pSamResults = samOutput.getPlugInArgValue<RasterElement>("Sam Results");
		if (pSamResults == NULL)
		{
			progress.report("SAM failed to return valid results.", 0, ERRORS, true);
			return NULL;
		}

		// Retrieve the SAM pseudocolor result layer.
		LayerList* pLayerList = pView->getLayerList();
		if (pLayerList == NULL)
		{
			progress.report("Failed to access SAM results layer list.", 0, ERRORS, true);
			return NULL;
		}

		PseudocolorLayer* pSamLayer = dynamic_cast<PseudocolorLayer*>(pLayerList->getLayer(PSEUDOCOLOR, pSamResults));
		if (pSamLayer == NULL)
		{
			progress.report("Failed to access SAM results layer.", 0, ERRORS, true);
			return NULL;
		}
		return pSamLayer;
}

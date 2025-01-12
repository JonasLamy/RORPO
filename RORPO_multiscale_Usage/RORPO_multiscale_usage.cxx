/* Copyright (C) 2014 Odyssee Merveille
odyssee.merveille@gmail.com

    This software is a computer program whose purpose is to compute RORPO.
    This software is governed by the CeCILL-B license under French law and
    abiding by the rules of distribution of free software.  You can  use,
    modify and/ or redistribute the software under the terms of the CeCILL-B
    license as circulated by CEA, CNRS and INRIA at the following URL
    "http://www.cecill.info".

    As a counterpart to the access to the source code and  rights to copy,
    modify and redistribute granted by the license, users are provided only
    with a limited warranty  and the software's author,  the holder of the
    economic rights,  and the successive licensors  have only  limited
    liability.

    In this respect, the user's attention is drawn to the risks associated
    with loading,  using,  modifying and/or developing or reproducing the
    software by the user in light of its specific status of free software,
    that may mean  that it is complicated to manipulate,  and  that  also
    therefore means  that it is reserved for developers  and  experienced
    professionals having in-depth computer knowledge. Users are therefore
    encouraged to load and test the software's suitability as regards their
    requirements in conditions enabling the security of their systems and/or
    data to be ensured and,  more generally, to use and operate it in the
    same conditions as regards security.

    The fact that you are presently reading this means that you have had
    knowledge of the CeCILL-B license and that you accept its terms.
*/

#include <stdint.h>
#include <iostream>
#include <string>
#include <vector>
#include <sys/stat.h>

#include "Image/Image.hpp"
#include "Image/Image_IO_ITK.hpp"
#include "RORPO/RORPO_multiscale.hpp"

#ifdef SLICER_BINDING
  #include "RORPO_multiscale_usageCLP.h"
#else
  #include "docopt.h"
#endif
typedef uint16_t u_int16_t;

bool isDir(std::string fileName) {
  struct stat buf;
  stat(fileName.c_str(), &buf);
  return S_ISDIR(buf.st_mode);
}

// Split a string
std::vector<std::string> split(std::string str, char delimiter) {
  std::vector<std::string> internal;
  std::stringstream ss(str); // Turn the string into a stream.
  std::string tok;

  while(getline(ss, tok, delimiter)) {
    internal.push_back(tok);
  }
  return internal;
}

template<typename PixelType>
void normalize_and_write_output(std::string outputPath, bool verbose, Image3D<PixelType> multiscale) {

    // getting min and max from multiscale image
    PixelType min = 255;
    PixelType max = 0;

    for (auto value: multiscale.get_data()) {
        if (value > max)
            max = value;
        if (value < min)
            min = value;
    }

    // normalize output
    Image3D<double> multiscale_normalized(multiscale.dimX(),
                                         multiscale.dimY(),
                                         multiscale.dimZ(),
                                         multiscale.spacingX(),
                                         multiscale.spacingY(),
                                         multiscale.spacingZ(),
                                         multiscale.originX(),
                                         multiscale.originY(),
                                         multiscale.originZ()
    );

    if (max - min == 0)
        max = min + 1;

    for (unsigned int i = 0; i < multiscale.size(); i++) {
        multiscale_normalized.get_data()[i] = (multiscale.get_data()[i] - min) / (double) (max - min);
    }

    if (verbose) {
        std::cout << "converting output image intensity : " << (int) min << "-" << (int) max << " to [0,1]"
                  << std::endl;
    }

    // Write the result to nifti image
    Write_Itk_Image<double>(multiscale_normalized, outputPath);
}

template<typename PixelType>
int RORPO_multiscale_usage(Image3D<PixelType> &image,
                           std::string outputVolume,
                           std::vector<int> &scaleList,
                           std::vector<int> &window,
                           int nbCores,
                           int dilationSize,
                           bool verbose,
                           bool normalize,
                           std::string maskVolume) {
    unsigned int dimz = image.dimZ();
    unsigned int dimy = image.dimY();
    unsigned int dimx= image.dimX();

    float  spacingX = image.spacingX();
    float  spacingY = image.spacingY();
    float  spacingZ = image.spacingZ();

    if (verbose){
        std::cout << "dimensions: [" << dimx << ", " << dimy << ", " << dimz << "]" << std::endl;
        std::cout << "spacing: [" << spacingX << ", " << spacingY << ", " << spacingZ << "]" << std::endl;
	}

    // ------------------ Compute input image intensity range ------------------

    std::pair<PixelType,PixelType> minmax = image.min_max_value();

    if (verbose){
        std::cout<< "Image intensity range: "<< (int)minmax.first << ", "
                 << (int)minmax.second << std::endl;
        std::cout<<std::endl;
	}

    // ------------------------ Negative intensities -----------------------
    if (minmax.first < 0)
    {
        std::cerr << "Image contains negative values" << std::endl;
        return 1;
    }

    // -------------------------- mask Image -----------------------------------

    Image3D<uint8_t> mask;

    if (!maskVolume.empty()) // A mask image is given
	{
        mask = Read_Itk_Image<uint8_t>(maskVolume);

        if (mask.dimX() != dimx || mask.dimY() != dimy || mask.dimZ() != dimz){
            std::cerr<<"Size of the mask image (dimx= "<<mask.dimX()
                    <<" dimy= "<<mask.dimY()<<" dimz="<<mask.dimZ()
                   << ") is different from size of the input image"<<std::endl;
            return 1;
        }
    }

    // #################### Convert input image to char #######################

    if (window[2] > 0 || typeid(PixelType) == typeid(float) ||
            typeid(PixelType) == typeid(double))
    {
        if (window[2] == 2 || minmax.first > (PixelType) window[0])
            window[0] = minmax.first;

        if (window[2] == 2 || minmax.second < (PixelType) window[1])
            window[1] = minmax.second;

        if(verbose){
            std::cout<<"Convert image intensity range from: [";
            std::cout<<minmax.first<<", "<<minmax.second<<"] to [";
            std::cout<<window[0]<<", "<<window[1]<<"]"<<std::endl;
        }

        image.window_dynamic(window[0], window[1]);

        if(verbose)
            std::cout << "Convert image to uint8" << std::endl;

        minmax.first = 0;
        minmax.second = 255;
        Image3D<uint8_t> imageChar = image.copy_image_2_uchar();

        // Run RORPO multiscale
        Image3D<uint8_t> multiscale =
                RORPO_multiscale<uint8_t, uint8_t>(imageChar,
                                                   scaleList,
                                                   nbCores,
                                                   dilationSize,
                                                   verbose,
                                                   mask);
        if (normalize)
            normalize_and_write_output<uint8_t>(outputVolume, verbose, multiscale);
        else
            Write_Itk_Image<uint8_t>(multiscale, outputVolume);
    }

    // ################## Keep input image in PixelType ########################

    else {

        // Run RORPO multiscale
        Image3D<PixelType> multiscale =
                RORPO_multiscale<PixelType, uint8_t>(image,
                                                     scaleList,
                                                     nbCores,
                                                     dilationSize,
                                                     verbose,
                                                     mask);

        // normalize output
        if (normalize)
            normalize_and_write_output(outputVolume, verbose, multiscale);
        else
            Write_Itk_Image<PixelType>(multiscale, outputVolume);
    }

    return 0;
} // RORPO_multiscale_usage

#ifndef SLICER_BINDING
// Parse command line with docopt


static const char USAGE[] =
R"(RORPO_multiscale_usage.

    USAGE:
    RORPO_multiscale_usage --input=ImagePath --output=OutputPath --scaleMin=MinScale --factor=F --nbScales=NBS [--window=min,max] [--nbCores=nbCores] [--dilationSize=Size] [--mask=maskVolume] [--verbose] [--normalize] [--uint8] [--series]

    Options:
         --nbCores=<nbCores>      Number of CPUs used for RPO computation \
         --dilationSize=<Size> Size of the dilation for the noise robustness step \
         --window=min,max      Convert intensity range [min, max] of the input \
                               image to [0,255] and convert to uint8 image\
                               (strongly decrease computation time).
         --mask=maskVolume       Path to a mask for the input image \
                               (0 for the background; not 0 for the foreground).\
                               mask image type must be uint8.
         --verbose             Activation of a verbose mode.
         --dicom               Specify that <imagePath> is a DICOM image.
         --normalize           Return a double normalized output image
         --uint8               Convert input image into uint8.
        )";
#endif

int main(int argc, char **argv) {
  #ifdef SLICER_BINDING
  PARSE_ARGS;
  #endif

   #ifndef SLICER_BINDING
    // -------------- Parse arguments and initialize parameters ----------------
    std::map<std::string, docopt::value> args = docopt::docopt(USAGE,
                                                  {argv + 1, argv + argc},
                                                  true,
                                                  "RORPO_multiscale_usage 2.0");

    std::cout<<" "<<std::endl;
    std::cout<<"Parameters: "<<std::endl;
    for(auto const& arg : args) {
        std::cout << arg.first << ": " << arg.second << std::endl;
    }

    std::string inputVolume = args["--input"].asString();
    std::string outputVolume = args["--output"].asString();
    float scaleMin = std::stoi(args["--scaleMin"].asString());
    float factor = std::stof(args["--factor"].asString());
    int nbScales = std::stoi(args["--nbScales"].asString());
    std::vector<int> window(3);
    int nbCores = 1;
    int dilationSize = 3;
    std::string maskVolume;
    bool verbose = args["--verbose"].asBool();
    bool normalize = args["--normalize"].asBool();
    
    if (args["--mask"])
        maskVolume = args["--mask"].asString();

    if (args["--core"])
        nbCores = std::stoi(args["--core"].asString());

    if(args["--dilationSize"])
        dilationSize = std::stoi(args["--dilationSize"].asString());

    if(verbose)
        std::cout<<"dilation size:"<<dilationSize<<std::endl;

    if (args["--window"]){
        std::vector<std::string> windowVector =
                split(args["--window"].asString(),',');

        window[0] = std::stoi(windowVector[0]);
        window[1] = std::stoi(windowVector[1]);
        window[2] = 1; // --window used
    } else if (args["--uint8"].asBool())
        window[2] = 2; // convert input image to uint8
    else
        window[2] = 0; // --window not used
    #endif

    // -------------------------- Scales computation ---------------------------

    std::vector<int> scaleList(nbScales);
    scaleList[0] = scaleMin;

    for (int i = 1; i < nbScales; ++i)
        scaleList[i] = int(scaleMin * pow(factor, i));

    if (verbose){
        std::cout << "Scales : ";
        std::cout << scaleList[0];
        for (int i = 1; i < nbScales; ++i)
            std::cout << ',' << scaleList[i];
        std::cout << std::endl;
    }

    // -------------------------- Read ITK Image -----------------------------
    auto res = Read_Itk_Metadata(inputVolume);
    if (res == std::nullopt)
        return 1;
    Image3DMetadata imageMetadata = *res;
    // ---------------- Find image type and run RORPO multiscale ---------------
    int error;
    if (verbose){
        std::cout<<" "<<std::endl;
        std::cout << "------ INPUT IMAGE -------" << std::endl;
        std::cout << "Input image type: " << imageMetadata.pixelTypeString << std::endl;
    }

    if ( imageMetadata.nbDimensions != 3 ) {
        std::cerr << "Error: input image dimension is " << imageMetadata.nbDimensions << " but should be 3 " << std::endl;
        return 1;
    }

    std::error_code ec; // For using the non-throwing overloads of functions below.
    bool dicom = isDir(inputVolume); // check if path is a directory (DICOM) of a file (.nii,.nrrd,etc.)
    switch (imageMetadata.pixelType){
        case itk::ImageIOBase::UCHAR:
        {
            Image3D<unsigned char> image = dicom?Read_Itk_Image_Series<unsigned char>(inputVolume):Read_Itk_Image<unsigned char>(inputVolume);
            error = RORPO_multiscale_usage<unsigned char>(image,
                                                          outputVolume,
                                                          scaleList,
                                                          window,
                                                          nbCores,
                                                          dilationSize,
                                                          verbose,
                                                          normalize,
                                                          maskVolume);
            break;
        }
        case itk::ImageIOBase::CHAR:
        {
            Image3D<char> image = dicom?Read_Itk_Image_Series<char>(inputVolume):Read_Itk_Image<char>(inputVolume);
            error = RORPO_multiscale_usage<char>(image,
                                                 outputVolume,
                                                 scaleList,
                                                 window,
                                                 nbCores,
                                                 dilationSize,
                                                 verbose,
                                                 normalize,
                                                 maskVolume);
            break;
        }
        case itk::ImageIOBase::USHORT:
        {
            Image3D<unsigned short> image = dicom?Read_Itk_Image_Series<unsigned short>(inputVolume):Read_Itk_Image<unsigned short>(inputVolume);
            error = RORPO_multiscale_usage<unsigned short>(image,
                                                           outputVolume,
                                                           scaleList,
                                                           window,
                                                           nbCores,
                                                           dilationSize,
                                                           verbose,
                                                           normalize,
                                                           maskVolume);
            break;
        }
        case itk::ImageIOBase::SHORT:
        {
            Image3D<short> image = dicom?Read_Itk_Image_Series<short>(inputVolume):Read_Itk_Image<short>(inputVolume);
            error = RORPO_multiscale_usage<short>(image,
                                                  outputVolume,
                                                  scaleList,
                                                  window,
                                                  nbCores,
                                                  dilationSize,
                                                  verbose,
                                                  normalize,
                                                  maskVolume);
            break;
        }
        case itk::ImageIOBase::UINT:
        {
            Image3D<unsigned int> image = dicom?Read_Itk_Image_Series<unsigned int>(inputVolume):Read_Itk_Image<unsigned int>(inputVolume);
            error = RORPO_multiscale_usage<unsigned int>(image,
                                                         outputVolume,
                                                         scaleList,
                                                         window,
                                                         nbCores,
                                                         dilationSize,
                                                         verbose,
                                                         normalize,
                                                         maskVolume);
            break;
        }
        case itk::ImageIOBase::INT:
        {
            Image3D<int> image = dicom?Read_Itk_Image_Series<int>(inputVolume):Read_Itk_Image<int>(inputVolume);
            error = RORPO_multiscale_usage<int>(image,
                                                outputVolume,
                                                scaleList,
                                                window,
                                                nbCores,
                                                dilationSize,
                                                verbose,
                                                normalize,
                                                maskVolume);
            break;
        }
        case itk::ImageIOBase::ULONG:
        {
            Image3D<unsigned long> image = dicom?Read_Itk_Image_Series<unsigned long>(inputVolume):Read_Itk_Image<unsigned long>(inputVolume);
            error = RORPO_multiscale_usage<unsigned long>(image,
                                                          outputVolume,
                                                          scaleList,
                                                          window,
                                                          nbCores,
                                                          dilationSize,
                                                          verbose,
                                                          normalize,
                                                          maskVolume);
            break;
        }
        case itk::ImageIOBase::LONG:
        {
            Image3D<long> image = dicom?Read_Itk_Image_Series<long>(inputVolume):Read_Itk_Image<long>(inputVolume);
            error = RORPO_multiscale_usage<long>(image,
                                                 outputVolume,
                                                 scaleList,
                                                 window,
                                                 nbCores,
                                                 dilationSize,
                                                 verbose,
                                                 normalize,
                                                 maskVolume);
            break;
        }
#ifdef ITK_SUPPORTS_LONGLONG
	case itk::ImageIOBase::ULONGLONG:
        {
            Image3D<unsigned long long> image = dicom?Read_Itk_Image_Series<unsigned long long>(inputVolume):Read_Itk_Image<unsigned long long>(inputVolume);
            error = RORPO_multiscale_usage<unsigned long long>(image,
                                                               outputVolume,
                                                               scaleList,
                                                               window,
                                                               nbCores,
                                                               dilationSize,
                                                               verbose,
                                                               normalize,
                                                               maskVolume);
            break;
        }
        case itk::ImageIOBase::LONGLONG:
        {
            Image3D<long long> image = dicom?Read_Itk_Image_Series<long long>(inputVolume):Read_Itk_Image<long long>(inputVolume);
            error = RORPO_multiscale_usage<long long>(image,
                                                      outputVolume,
                                                      scaleList,
                                                      window,
                                                      nbCores,
                                                      dilationSize,
                                                      verbose,
                                                      normalize,
                                                      maskVolume);
            break;
        }
#endif // ITK_SUPPORTS_LONGLONG
        case itk::ImageIOBase::FLOAT:
        {
            Image3D<float> image = dicom?Read_Itk_Image_Series<float>(inputVolume):Read_Itk_Image<float>(inputVolume);
            error = RORPO_multiscale_usage<float>(image,
                                                  outputVolume,
                                                  scaleList,
                                                  window,
                                                  nbCores,
                                                  dilationSize,
                                                  verbose,
                                                  normalize,
                                                  maskVolume);
            break;
        }
        case itk::ImageIOBase::DOUBLE:
        {
            Image3D<double> image = dicom?Read_Itk_Image_Series<double>(inputVolume):Read_Itk_Image<double>(inputVolume);
            error = RORPO_multiscale_usage<double>(image,
                                                   outputVolume,
                                                   scaleList,
                                                   window,
                                                   nbCores,
                                                   dilationSize,
                                                   verbose,
                                                   normalize,
                                                   maskVolume);
            break;
        }
        case itk::ImageIOBase::UNKNOWNCOMPONENTTYPE:
        default:
            error = 1;
            std::cout << "Error: pixel type unknown." << std::endl;
            break;
    }
    return error;
}//end main

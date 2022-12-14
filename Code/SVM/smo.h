/*
* The information in this file is
* Copyright(c) 2012, Himanshu Singh <91.himanshu@gmail.com>
* and is subject to the terms and conditions of the
* GNU Lesser General Public License Version 2.1
* The license text is available from   
* http://www.gnu.org/licenses/lgpl.html
*/

#ifndef SMO_H
#define SMO_H

#include "Progress.h"
#include "svm.h"
#include "svmModel.h"

#include <vector>
#include <string>
using std::string;
using std::vector;

class SMO
{
public:
    SMO(SVM* _plugin, double _c, double _sigma, double _eps, double _tolerance, string& _kernelType,
        string& _class, vector<point>& _points, vector<int>& _target, vector<point>& _testSet, vector<int>& _yTest,
        vector<point>& _cvSet, vector<int>& _yCV) : 
        plugin(_plugin),
        C(_c),
        sigma(_sigma),
        epsilon(_eps),
        tolerance(_tolerance),
        kernelType(_kernelType),
        className(_class),
        points(_points),
        target(_target),
        testSet(_testSet),
        yTest(_yTest),
        crossValidationSet(_cvSet),
        yCV(_yCV)
    {}

    svmModel train();

private:
    SVM* plugin;

    double predict(const point&);
    double kernel(const point&, const point&);

    void normalizeFeatures();

    int takeStep(int, int);
    int examineExample(int);

    // Parameters required to run SMO
    double C;
    double sigma;
    double epsilon;
    double tolerance;
    string kernelType;
    string className;
    // The train set on which SMO will be trained.
    vector<point> points;
    vector<int> target;
    // Cross validation set. Use it to obtain correct parameter C and sigma for the model.
    vector<point> crossValidationSet;
    vector<int> yCV;
    // Test set to determine the accuracy of model on unseen data.
    vector<point> testSet;
    vector<int> yTest;
    // Parameters maipulated by SMO
    vector<double> w;
    vector<double> alpha;
    double threshold;
    vector<double> errorCache;
    // Mean and Standard Deviation for each feature.
    vector<double> mu, stdv;
};

#endif




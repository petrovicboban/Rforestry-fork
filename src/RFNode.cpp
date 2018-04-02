#include "RFNode.h"
#include <RcppEigen.h>
#include <mutex>
#include <thread>
#include "utils.h"

std::mutex mutex_weightMatrix;


RFNode::RFNode():
  _averagingSampleIndex(nullptr), _splittingSampleIndex(nullptr),
  _splitFeature(0), _splitValue(0),
  _leftChild(nullptr), _rightChild(nullptr),
  _averageCount(0), _splitCount(0) {}

RFNode::~RFNode() {
  //  std::cout << "RFNode() destructor is called." << std::endl;
};

void RFNode::setLeafNode(
  std::unique_ptr< std::vector<size_t> > averagingSampleIndex,
  std::unique_ptr< std::vector<size_t> > splittingSampleIndex
) {
  if (
      (*averagingSampleIndex).size() == 0 &&
        (*splittingSampleIndex).size() == 0
  ) {
    throw std::runtime_error("Intend to create an empty node.");
  }
  // Give the ownership of the index pointer to the RFNode object
  this->_averagingSampleIndex = std::move(averagingSampleIndex);
  this->_averageCount = (*_averagingSampleIndex).size();
  this->_splittingSampleIndex = std::move(splittingSampleIndex);
  this->_splitCount = (*_splittingSampleIndex).size();
}

void RFNode::setSplitNode(
  size_t splitFeature,
  double splitValue,
  std::unique_ptr< RFNode > leftChild,
  std::unique_ptr< RFNode > rightChild
) {
  // Split node constructor
  _averageCount = 0;
  _splitCount = 0;
  _splitFeature = splitFeature;
  _splitValue = splitValue;
  // Give the ownership of the child pointer to the RFNode object
  _leftChild = std::move(leftChild);
  _rightChild = std::move(rightChild);
}


void RFNode::predict(
  std::vector<float> &outputPrediction,
  std::vector<size_t>* updateIndex,
  std::vector< std::vector<float> >* xNew,
  DataFrame* trainingData,
  Eigen::MatrixXf* weightMatrix
) {

  // If the node is a leaf, aggregate all its averaging data samples
  if (is_leaf()) {

    // Calculate the mean of current node
    float predictedMean = (*trainingData).partitionMean(getAveragingIndex());

    // Give all updateIndex the mean of the node as prediction values
    for (
      std::vector<size_t>::iterator it = (*updateIndex).begin();
      it != (*updateIndex).end();
      ++it
    ) {
      outputPrediction[*it] = predictedMean;
    }

    if(weightMatrix){
      // If weightMatrix is not a NULL pointer, then we want to update it,
      // because we have choosen aggregation = "weightmatrix".
      std::vector<size_t> idx_in_leaf =
        (*trainingData).get_all_row_idx(getAveragingIndex());
      // The following will lock the access to weightMatrix
      std::lock_guard<std::mutex> lock(mutex_weightMatrix);
      for (
          std::vector<size_t>::iterator it = (*updateIndex).begin();
          it != (*updateIndex).end();
          ++it ) {
        for (size_t i = 0; i<idx_in_leaf.size(); i++) {
          (*weightMatrix)(*it, idx_in_leaf[i] - 1) +=
                        (double) 1.0 / idx_in_leaf.size();
        }
      }
    }

  } else {

    // Separate prediction tasks to two children
    std::vector<size_t>* leftPartitionIndex = new std::vector<size_t>();
    std::vector<size_t>* rightPartitionIndex = new std::vector<size_t>();

    // Test if the splitting feature is categorical
    std::vector<size_t> categorialCols = *(*trainingData).getCatCols();
    if (
      std::find(
        categorialCols.begin(),
        categorialCols.end(),
        getSplitFeature()
      ) != categorialCols.end()
    ){

      // If the splitting feature is categorical, split by (==) or (!=)
      for (
        std::vector<size_t>::iterator it = (*updateIndex).begin();
        it != (*updateIndex).end();
        ++it
      ) {
        if ((*xNew)[getSplitFeature()][*it] == getSplitValue()) {
          (*leftPartitionIndex).push_back(*it);
        } else {
          (*rightPartitionIndex).push_back(*it);
        }
      }

    } else {

      // For non-categorical, split to left (<) and right (>=) according to the
      // split value
      for (
        std::vector<size_t>::iterator it = (*updateIndex).begin();
        it != (*updateIndex).end();
        ++it
      ) {
        if ((*xNew)[getSplitFeature()][*it] < getSplitValue()) {
          (*leftPartitionIndex).push_back(*it);
        } else {
          (*rightPartitionIndex).push_back(*it);
        }
      }

    }

    // Recursively get predictions from its children
    if ((*leftPartitionIndex).size() > 0) {
      (*getLeftChild()).predict(
        outputPrediction,
        leftPartitionIndex,
        xNew,
        trainingData,
        weightMatrix
      );
    }
    if ((*rightPartitionIndex).size() > 0) {
      (*getRightChild()).predict(
        outputPrediction,
        rightPartitionIndex,
        xNew,
        trainingData,
        weightMatrix
      );
    }

    delete(leftPartitionIndex);
    delete(rightPartitionIndex);

  }

}

bool RFNode::is_leaf() {
  return !(getAverageCount() == 0 && getSplitCount() == 0);
}

void RFNode::printSubtree(int indentSpace) {

  // Test if the node is leaf node
  if (is_leaf()) {

    // Print count of samples in the leaf node
    std::cout << std::string((unsigned long) indentSpace, ' ')
              << "Leaf Node: # of split samples = "
              << getSplitCount()
              << ", # of average samples = "
              << getAverageCount()
              << std::endl;

  } else {

    // Print split feature and split value
    std::cout << std::string((unsigned long) indentSpace, ' ')
              << "Tree Node: split feature = "
              << getSplitFeature()
              << ", split value = "
              << getSplitValue()
              << std::endl;
    // Recursively calling its children
    (*getLeftChild()).printSubtree(indentSpace+2);
    (*getRightChild()).printSubtree(indentSpace+2);

  }
}

// -----------------------------------------------------------------------------

void RFNode::write_node_info(
    std::unique_ptr<tree_info> const & treeInfo
){
  if (is_leaf()) {
    // If it is a leaf: set everything to be 0
    treeInfo->var_id.push_back(0);
    treeInfo->split_val.push_back(0);

  } else {
    // If it is a usual node: remember split var and split value and recursively
    // call write_node_info on the left and the right child.
    treeInfo->var_id.push_back(getSplitFeature() + 1);
    treeInfo->split_val.push_back(getSplitValue());

    getLeftChild()->write_node_info(treeInfo);
    getRightChild()->write_node_info(treeInfo);
  }
  return;
}

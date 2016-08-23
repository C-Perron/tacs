#include "TACSAssembler.h"

// Reordering implementation 
#include "FElibrary.h"
#include "MatUtils.h"
#include "AMDInterface.h"

// Include the AMD package if we have it
#include "amd.h"

// The TACS-METIS header
#include "tacsmetis.h"

// TACS-BLAS/LAPACK header
#include "tacslapack.h"

/*
  TACSAssembler implementation

  Copyright (c) 2010-2016 Graeme Kennedy. All rights reserved. 
  Not for commercial purposes.
*/

/*
  Constructor for the TACSAssembler object

  input:
  tacs_comm:           the TACS communicator 
  varsPerNode:         the number of degrees of freedom per node
  numOwnedNodes:       the number of locally-owned nodes
  numElements:         the number of elements in the mesh
  numDependentNodes:   the number of dependent nodes in the mesh
*/
TACSAssembler::TACSAssembler( MPI_Comm _tacs_comm,
                              int _varsPerNode, int _numOwnedNodes, 
			      int _numElements, int _numDependentNodes ){
  TacsInitialize();
  
  // Copy the communicator for MPI communication
  tacs_comm = _tacs_comm;

  // If MPI is being used, get the rank of the current 
  // process and the total number of processes
  MPI_Comm_rank(tacs_comm, &mpiRank);
  MPI_Comm_size(tacs_comm, &mpiSize);

  // Set the simulation time to 0
  time = 0.0;

  // Now set up the default pthread info with 1 thread
  thread_info = new TACSThreadInfo(1); 
  thread_info->incref();
  pthread_mutex_init(&tacs_mutex, NULL);

  // Create the class that is used to 
  tacsPInfo = new TACSAssemblerPthreadInfo();
  numCompletedElements = 0;

  // copy data to be used later in the program
  varsPerNode = _varsPerNode;
  numElements = _numElements;
  numOwnedNodes = _numOwnedNodes;
  numDependentNodes = _numDependentNodes;

  // These values will be computed later
  numExtNodes = 0;
  numNodes = 0;
  extNodeOffset = 0;

  // Print out the number of local nodes and elements
  printf("[%d] Creating TACSAssembler with numOwnedNodes = %d \
numElements = %d\n", mpiRank, numOwnedNodes, numElements);

  // Calculate the total number of nodes and elements
  int info[2], recv_info[2];
  info[0] = numOwnedNodes;
  info[1] = numElements;
  MPI_Reduce(info, recv_info, 2, MPI_INT, MPI_SUM, 0, tacs_comm);
  if (mpiSize > 1 && mpiRank == 0){
    printf("[%d] TACSAssembler: Total dof = %d Total nodes = %d \
Total elements = %d\n", mpiRank, varsPerNode*recv_info[0], 
           recv_info[0], recv_info[1]);
  }

  // Initialize some information about the number of items
  meshInitializedFlag = 0;
  maxElementNodes = 0;
  maxElementSize = 0;
  maxElementIndepNodes = 0;

  // Set the elements array to NULL
  elements = NULL;

  // Set the auxiliary element class to NULL
  auxElements = NULL;

  // Information for setting boundary conditions and distributing variables
  varMap = new TACSVarMap(tacs_comm, numOwnedNodes);
  varMap->incref();

  // Estimate 100 bcs at first, but this is expanded as required
  int nbc_est = 100; 
  bcMap = new TACSBcMap(varsPerNode, nbc_est);
  bcMap->incref();

  // Set the internal vector values to NULL
  varsVec = NULL;
  dvarsVec = NULL;
  ddvarsVec = NULL;
  xptVec = NULL;

  // Set the external node numbers to NULL
  tacsExtNodeNums = NULL;

  // Set the distribution object to NULL at first
  extDist = NULL;
  extDistIndices = NULL;

  // Reordering information old node i -> newNodeIndices[i]
  newNodeIndices = NULL;

  // DistMat specific objects
  distMatIndices = NULL;

  // FEMat-specific objects
  feMatBIndices = feMatCIndices = NULL;
  feMatBMap = feMatCMap = NULL;
  
  // Allocate element-> node information
  elementNodeIndex = NULL;
  elementTacsNodes = NULL;

  // Null out the dependent node data
  depNodes = NULL;

  // Set the local element data to NULL
  elementData = NULL;
  elementIData = NULL;
}

/*
  The TACSAssembler destructor. 

  Clean up the allocated memory and decref() all objects
*/
TACSAssembler::~TACSAssembler(){
  TacsFinalize();

  pthread_mutex_destroy(&tacs_mutex);
  delete tacsPInfo;

  // Go through and decref all the elements
  if (elements){
    for ( int i = 0; i < numElements; i++ ){
      if (elements[i]){
        elements[i]->decref();
      }
    }
    delete [] elements;
  }

  // Decref the variables/node vectors
  if (varsVec){ varsVec->decref(); }
  if (dvarsVec){ dvarsVec->decref(); }
  if (ddvarsVec){ ddvarsVec->decref(); }
  if (xptVec){ xptVec->decref(); }

  // Delete nodal information
  if (elementNodeIndex){ delete [] elementNodeIndex; }
  if (elementTacsNodes){ delete [] elementTacsNodes; }
  if (tacsExtNodeNums){ delete [] tacsExtNodeNums; }  

  // Decreate the ref. count for the dependent node information
  if (depNodes){ depNodes->decref(); }

  // Decrease the reference count to the auxiliary elements
  if (auxElements){ auxElements->decref(); }

  // Decrease the reference count to objects allocated in initialize
  if (varMap){ varMap->decref(); }
  if (bcMap){ bcMap->decref(); }
  if (extDist){ extDist->decref(); }
  if (extDistIndices){ extDistIndices->decref(); }

  // Free the reordering if it has been used
  if (newNodeIndices){ newNodeIndices->decref(); }

  // Decrease ref. count for DistMat data
  if (distMatIndices){ distMatIndices->decref(); }

  // Decrease ref. count for the FEMat data if it is allocated
  if (feMatBIndices){ feMatBIndices->decref(); }
  if (feMatCIndices){ feMatCIndices->decref(); }
  if (feMatBMap){ feMatBMap->decref(); }
  if (feMatCMap){ feMatCMap->decref(); }

  // Delete arrays allocated in initializeArrays()
  if (elementData){ delete [] elementData; }
  if (elementIData){ delete [] elementIData; }

  // Decref the thread information class
  thread_info->decref();
}

const char * TACSAssembler::tacsName = "TACSAssembler";

/*
  Return the MPI communicator for the TACSAssembler object
*/
MPI_Comm TACSAssembler::getMPIComm(){
  return tacs_comm;
}

/*
  Get the number of unknowns per node
*/
int TACSAssembler::getVarsPerNode(){ 
  return varsPerNode; 
}

/*
  Get the number of local nodes
*/ 
int TACSAssembler::getNumNodes(){ 
  return numNodes; 
}

/*
  Get the number of dependent nodes
*/
int TACSAssembler::getNumDependentNodes(){ 
  return numDependentNodes; 
}

/*
  Get the number of elements
*/
int TACSAssembler::getNumElements(){ 
  return numElements; 
}

/*
  Get the node-processor assignment
*/
TACSVarMap *TACSAssembler::getVarMap(){ 
  return varMap; 
}

/*
  Get the boundary conditions
*/
TACSBcMap *TACSAssembler::getBcMap(){ 
  return bcMap; 
}

/*
  Get the external indices
*/
TACSBVecDistribute *TACSAssembler::getBVecDistribute(){ 
  return extDist; 
}

/*
  Get the array of elements from TACSAssembler
*/
TACSElement **TACSAssembler::getElements(){ 
  return elements; 
}

/*!
  Set the element connectivity.

  Note that the number of elements that are set at this stage must be
  consistent with the number of elements passed in when TACSAssembler
  is created.  (The connectivity arrays are copied and should be freed
  by the caller.)

  input:
  conn:   the connectivity from elements to global node index
  ptr:    offset into the connectivity array for this processor
*/
int TACSAssembler::setElementConnectivity( const int *conn,
                                           const int *ptr ){
  if (meshInitializedFlag){
    fprintf(stderr, 
            "[%d] Cannot call setElementConnectivity() after initialize()\n", 
            mpiRank);
    return 1;
  }  
  if (tacsExtNodeNums){
    fprintf(stderr,
            "[%d] Cannot call setElementConnectivity() after reordering\n",
            mpiRank);
    return 1;
  }

  // Free the data if it has already been set before
  if (elementTacsNodes){ delete [] elementTacsNodes; }
  if (elementNodeIndex){ delete [] elementNodeIndex; }

  int size = ptr[numElements];
  elementNodeIndex = new int[ numElements+1 ] ;
  elementTacsNodes = new int[ size ];
  memcpy(elementNodeIndex, ptr, (numElements+1)*sizeof(int));

  // Copy the node numbers
  memcpy(elementTacsNodes, conn, size*sizeof(int));

  // If the elements are set, check the connectivity
  if (elements){
    for ( int i = 0; i < numElements; i++ ){
      int size = elementNodeIndex[i+1] - elementNodeIndex[i];
      if (size != elements[i]->numNodes()){
        fprintf(stderr, 
                "[%d] Element %s does not match connectivity\n",
                mpiRank, elements[i]->elementName());
        return 1;
      }
    }
  }

  // Check that the node numbers are all within range and that the
  // dependent node numbers (if any) are in range
  const int *ownerRange;
  varMap->getOwnerRange(&ownerRange);

  for ( int i = 0; i < numElements; i++ ){
    int jend = elementNodeIndex[i+1];
    for ( int j = elementNodeIndex[i]; j < jend; j++ ){
      if (elementTacsNodes[j] >= ownerRange[mpiSize]){
        fprintf(stderr, 
                "[%d] Element %d contains node number out of range\n",
                mpiRank, i);
        return -1;
      }
      else if (elementTacsNodes[j] < -numDependentNodes){
        fprintf(stderr, 
                "[%d] Element %d contains dependent node number out of range\n",
                mpiRank, i);
        return -1;
      }
    }
  }
}

/*!
  Set the element array within TACS. 
  
  The number of element pointers provided should be equal to the
  number of elements specified in the TACSAssembler constructor.  Note
  that the elements themselves are not copied, just the pointers to
  them.

  input:
  elements:  the array of element pointers with length = numElements.
*/
int TACSAssembler::setElements( TACSElement **_elements ){
  if (meshInitializedFlag){
    fprintf(stderr, "[%d] Cannot call setElements() after initialize()\n", 
	    mpiRank);
    return 1;
  }

  if (elements){
    for ( int i = 0; i < numElements; i++ ){
      _elements[i]->incref();
      if (elements[i]){ elements[i]->decref(); }
      elements[i] = _elements[i];
    }
  }
  else {
    elements = new TACSElement*[ numElements ];
    memset(elements, 0, numElements*sizeof(TACSElement*));

    for ( int i = 0; i < numElements; i++ ){
      _elements[i]->incref();
      elements[i] = _elements[i];
    }
  }

  // Determine the maximum number of nodes per element
  maxElementSize = 0;
  maxElementNodes = 0;

  for ( int i = 0; i < numElements; i++ ){
    // Check if the number of variables per node matches
    if (_elements[i]->numDisplacements() != varsPerNode){
      fprintf(stderr, 
              "[%d] Element %s does not match variables per node\n",
              mpiRank, _elements[i]->elementName());
      return 1;
    }

    // Determine if the maximum number of variables and nodes needs to
    // be changed
    int elemSize = _elements[i]->numVariables();
    if (elemSize > maxElementSize){
      maxElementSize = elemSize;
    }

    elemSize = _elements[i]->numNodes();
    if (elemSize > maxElementNodes){
      maxElementNodes = elemSize;
    }
  }

  // If the connectivity is set, determine if it is consistent
  if (elementNodeIndex){
    for ( int i = 0; i < numElements; i++ ){
      int size = elementNodeIndex[i+1] - elementNodeIndex[i];
      if (size != elements[i]->numNodes()){
        fprintf(stderr, 
                "[%d] Element %s does not match connectivity\n",
                mpiRank, elements[i]->elementName());
        return 1;
      }
    }
  }
  
  return 0;
}

/*
  Set the dependent node data structure

  The dependent nodes are designated by a negative index with a 1-base
  numbering scheme. This dependent node data structure is a sparse
  matrix with constant weights that designates the weights applied to
  the global independent nodes (storred in depNodeToTacs).

  input:
  depNodePtr:      the offset into the connectivity/weight arrays
  depNodeToTacs:   the independent global nodes
  depNodeWeights:  the weight applied to each independent node
*/
int TACSAssembler::setDependentNodes( const int *_depNodePtr, 
                                      const int *_depNodeToTacs,
                                      const double *_depNodeWeights ){
  if (meshInitializedFlag){
    fprintf(stderr, 
            "[%d] Cannot call setDependentNodes() after initialize()\n", 
	    mpiRank);
    return 1;
  }
  if (tacsExtNodeNums){
    fprintf(stderr,
            "[%d] Cannot call setDependentNodes() after reordering\n",
            mpiRank);
    return 1;
  }

  // Free the data if the dependent nodes have already been set
  if (depNodes){ depNodes->decref(); }

  // Get the ownership range of the nodes
  const int *ownerRange;
  varMap->getOwnerRange(&ownerRange);

  // Check that all the independent nodes are positive and are within an
  // allowable range
  for ( int i = 0; i < numDependentNodes; i++ ){
    for ( int jp = _depNodePtr[i]; jp < _depNodePtr[i+1]; jp++ ){
      if (_depNodeToTacs[jp] >= ownerRange[mpiSize]){
        fprintf(stderr, 
                "[%d] Dependent node %d contains node number out of range\n",
                mpiRank, i);
        return 1;
      }
      else if (_depNodeToTacs[jp] < 0){
        fprintf(stderr, 
                "[%d] Dependent node %d contains dependent node %d\n",
                mpiRank, i, _depNodeToTacs[jp]);
        return 1;
      }
    }
  }

  // Allocate the new memory and copy over the data
  int *depNodePtr = new int[ numDependentNodes+1 ];
  memcpy(depNodePtr, _depNodePtr, (numDependentNodes+1)*sizeof(int));

  int size = depNodePtr[numDependentNodes];
  int *depNodeToTacs = new int[ size ];
  memcpy(depNodeToTacs, _depNodeToTacs, size*sizeof(int));

  double *depNodeWeights = new double[ size ];
  memcpy(depNodeWeights, _depNodeWeights, size*sizeof(double));

  // Allocate the dependent node data structure
  depNodes = new TACSBVecDepNodes(numDependentNodes,
                                  &depNodePtr, &depNodeToTacs,
                                  &depNodeWeights);
  depNodes->incref();

  return 0;
}

/*!
  Set the boundary conditions object that will be associated with the
  vectors/matrices that are created using TACSAssembler.
*/
void TACSAssembler::addBCs( int nnodes, const int *nodes, 
                            int nbcs, const int *vars,
                            const TacsScalar *vals ){
  if (meshInitializedFlag){
    fprintf(stderr, "[%d] Cannot call addBC() after initialize()\n", 
	    mpiRank);
    return;
  }

  // Adjust the input to the addBC call. If no vars are specified,
  // set the number of boundary conditions equal to the number of
  // variables per node
  if (!vars || (nbcs < 0)){
    nbcs = varsPerNode;
  }

  // Get the ownership range
  const int *ownerRange;
  varMap->getOwnerRange(&ownerRange);

  // Add all the boundary conditions within the specified owner range
  for ( int i = 0; i < nnodes; i++ ){
    if (nodes[i] >= ownerRange[mpiRank] &&
        nodes[i] < ownerRange[mpiRank+1]){
      bcMap->addBC(nodes[i], nbcs, vars, vals);
    }
  }
}

/*
  Create a global vector of node locations
*/
TACSBVec *TACSAssembler::createNodeVec(){
  return new TACSBVec(varMap, TACS_SPATIAL_DIM, NULL, 
                      extDist, depNodes);
}

/*!
  Set the nodes from the node map object
*/
void TACSAssembler::setNodes( TACSBVec *X ){
  xptVec->copyValues(X);

  // Distribute the values at this point
  xptVec->beginDistributeValues();
  xptVec->endDistributeValues();
}

/*!
  Get the node locations from TACS
*/
void TACSAssembler::getNodes( TACSBVec *X ){
  X->copyValues(xptVec);
}

/*
  Set the auxiliary elements within the TACSAssembler object
  
  This only needs to be done once sometime during initialization.  If
  you need to change the loads repeatedly, this can be called
  repeatedly. No check is made at this point that you haven't done
  something odd. Note that the code assumes that the elements defined
  here perfectly overlap the non-zero pattern of the elements set
  internally within TACS already.
*/
void TACSAssembler::setAuxElements( TACSAuxElements *_aux_elems ){
  // Increase the reference count to the input elements (Note that
  // the input may be NULL to over-write the internal object
  if (_aux_elems){
    _aux_elems->incref();
  }

  // Decrease the reference count if the old object is not NULL
  if (auxElements){
    auxElements->decref();
  }
  auxElements = _aux_elems;

  // Check whether the auxiliary elements match
  if (auxElements){
    int naux = 0;
    TACSAuxElem *aux = NULL;
    naux = auxElements->getAuxElements(&aux);
    for ( int k = 0; k < naux; k++ ){
      int elem = aux[k].num;
      if (elements[elem]->numVariables() != 
          aux[k].elem->numVariables()){
        fprintf(stderr, "[%d] Auxiliary element sizes do not match\n",
                mpiRank);
      }
    }
  }
}

/*
  Retrieve the auxiliary element object from TACSAssembler

  Warning: The auxiliary element object may be NULL
*/
TACSAuxElements *TACSAssembler::getAuxElements(){
  return auxElements;
}

/*
  Compute the external list of nodes and sort these nodes

  This code is called automatically by TACSAssembler and is private.
  This code computes numExtNodes and allocates the array
  tacsExtNodeNums for use in other functions. This function should
  only be called once during reordering or during initialization.  
*/
int TACSAssembler::computeExtNodes(){
  if (meshInitializedFlag){
    fprintf(stderr, 
            "[%d] Cannot call computeExtNodes() after initialize()\n", 
	    mpiRank);
    return 1;
  }
  if (!elementNodeIndex){
    fprintf(stderr, "[%d] Cannot call computeExtNodes() element \
connectivity not defined\n", mpiRank);
    return 1;
  }

  // Get the ownership range of the nodes
  const int *ownerRange;
  varMap->getOwnerRange(&ownerRange);

  // Find the maximum possible size of array
  int max_size = elementNodeIndex[numElements];
  
  // Get the dependent node connectivity information (if any) and
  // increase the size of the array to account for any external dependent
  // nodes we may find.
  const int *depNodePtr = NULL;
  const int *depNodeConn = NULL;
  if (depNodes){
    depNodes->getDepNodes(&depNodePtr, &depNodeConn, NULL);
    max_size += depNodePtr[numDependentNodes];
  }

  // Keep track of the external nodes that we've found
  int ext_size = 0;
  int *ext_list = new int[ max_size ];

  // First loop over the element connectivity
  for ( int i = 0; i < elementNodeIndex[numElements]; i++ ){
    int node = elementTacsNodes[i];

    // Check if the node is external
    if ((node >= 0) && 
        (node < ownerRange[mpiRank] || 
         node >= ownerRange[mpiRank+1])){
      ext_list[ext_size] = node;
      ext_size++;
    }
  }

  // Loop over the dependent nodes
  if (depNodes){
    int end = depNodePtr[numDependentNodes];
    for ( int i = 0; i < end; i++ ){
      int node = depNodeConn[i];

      // Check if the node is external
      if ((node >= 0) && 
          (node < ownerRange[mpiRank] || 
           node >= ownerRange[mpiRank+1])){
        ext_list[ext_size] = node;
        ext_size++;
      }
    }
  }

  // Sort the list of nodes
  numExtNodes = FElibrary::uniqueSort(ext_list, ext_size);

  // Allocate an array of the external nodes that is tight
  // to the number of external nodes
  tacsExtNodeNums = new int[ numExtNodes ];
  memcpy(tacsExtNodeNums, ext_list, numExtNodes*sizeof(int));
  
  // Free the original list of external nodes;
  delete [] ext_list;

  // Now the total number of nodes is equal to the external nodes 
  // plus the locally owned nodes
  numNodes = numOwnedNodes + numExtNodes;

  // Find the offset into the external node list
  extNodeOffset = 0;
  while (extNodeOffset < numExtNodes &&
         tacsExtNodeNums[extNodeOffset] < ownerRange[mpiRank]){
    extNodeOffset++;
  }

  return 0;
}

/*!
  Compute a reordering of the nodes.

  This function should be called after the element connectivity,
  boundary conditions and optionally the dependent nodes are set, but
  before the call to initialize().

  This code computes and stores a reordering based on both the matrix
  type and the ordering type. The matrix type determines what
  constitutes a coupling node and what does not. The matrix type only
  impacts the ordering in parallel computations.

  The algorithm proceeds as follows:

  1. Compute the coupling nodes referenced by another processor
    
  2. Order the locally owned nodes based on the input ordering.
  
  3. Based on the recvied coupling nodes, set the outgoing node
  numbers back into the recieving array.
  
  4. Set the new values of the nodes on the requesting processes
*/
void TACSAssembler::computeReordering( enum OrderingType order_type,
                                       enum MatrixOrderingType mat_type ){
  // Return if the element connectivity not set
  if (!elementNodeIndex){
    fprintf(stderr, 
            "[%d] Must define element connectivity before reordering\n",
            mpiRank);
    return;
  }
  if (tacsExtNodeNums){
    fprintf(stderr, 
            "[%d] TACSAssembler::computeReordering() can only be called once\n",
            mpiRank);
    return;
  }

  // Compute the external nodes
  computeExtNodes();

  // Compute the local node numbers that correspond to the coupling
  // nodes between processors
  int *couplingNodes;
  int *extPtr, *extCount;
  int *recvPtr, *recvCount, *recvNodes;
  int numCoupling = 
    computeCouplingNodes(&couplingNodes, &extPtr, &extCount,
                         &recvPtr, &recvCount, &recvNodes);

  // The new node numbers to be set according to the different
  // re-ordering schemes
  int *newNodeNums = new int[ numNodes ];

  // Metis can't handle the non-zero diagonal in the CSR data structure
  int noDiagonal = 0;
  if (order_type == ND_ORDER){ 
    noDiagonal = 1;
  }

  // If using only one processor, order everything. In this case
  // there is no distinction between local and global ordering.
  if (mpiSize == 1){
    // The node connectivity
    int *rowp, *cols;
    computeLocalNodeToNodeCSR(&rowp, &cols, noDiagonal);
    computeMatReordering(order_type, numNodes, rowp, cols, 
                         NULL, newNodeNums);

    delete [] rowp;
    delete [] cols;
  }
  else {
    // First, find the reduced nodes - the set of nodes 
    // that are only referenced by this processor. These 
    // can be reordered without affecting other processors.
    int *reducedNodes = new int[ numNodes ];
    memset(reducedNodes, 0, numNodes*sizeof(int));

    // Add all the nodes that are external to this processor
    for ( int i = 0; i < extNodeOffset; i++ ){
      reducedNodes[i] = -1;
    }
    for ( int i = extNodeOffset + numOwnedNodes; i < numNodes; i++ ){
      reducedNodes[i] = -1;
    }

    // Depending on the matrix type, also add the external dependent
    // nodes and nodes that also couple to those nodes
    if (mat_type == DIRECT_SCHUR){
      for ( int i = 0; i < numCoupling; i++ ){
        int node = couplingNodes[i];
        reducedNodes[node] = -1;
      }
    }
    else if (mat_type == APPROXIMATE_SCHUR){
      // If we want an approximate schur ordering, where the
      // nodes that couple to other processors are ordered last,
      // we also add these nodes to the reduced set.
      int *rowp, *cols;
      computeLocalNodeToNodeCSR(&rowp, &cols);

      // Order all nodes linked by an equation to an external node
      // This ordering is required for the approximate Schur method
      for ( int i = 0; i < numCoupling; i++ ){
        int node = couplingNodes[i];
        for ( int jp = rowp[node]; jp < rowp[node+1]; jp++ ){
          reducedNodes[cols[jp]] = -1;
        }
      }

      delete [] rowp;
      delete [] cols;
    }

    // Now, order all nodes that are non-negative
    int numReducedNodes = 0;
    for ( int i = 0; i < numNodes; i++ ){
      if (reducedNodes[i] >= 0){
        reducedNodes[i] = numReducedNodes;
        numReducedNodes++;
      }
    }

    // Compute the reordering for the reduced set of nodes
    int *newReducedNodes = new int[ numReducedNodes ];
    int *rowp, *cols;
    computeLocalNodeToNodeCSR(&rowp, &cols, 
                              numReducedNodes, reducedNodes, 
                              noDiagonal);
    computeMatReordering(order_type, numReducedNodes, rowp, cols,
                         NULL, newReducedNodes);
    delete [] rowp;
    delete [] cols;

    // Place the result back into the newNodeNums - add the
    // ownership offset
    const int *ownerRange;
    varMap->getOwnerRange(&ownerRange);
    int offset = ownerRange[mpiRank];
    for ( int i = 0, j = 0; i < numNodes; i++ ){
      if (reducedNodes[i] >= 0){
        newNodeNums[i] = offset + newReducedNodes[j];
        j++;
      }
    }

    delete [] newReducedNodes;

    // Add the offset to the total number of reduced nodes
    offset += numReducedNodes; 

    // Now, order any remaining variables that have not yet been
    // ordered. These are the coupling variables (if any) that
    // have been labeled before.
    numReducedNodes = 0;
    for ( int i = extNodeOffset; i < extNodeOffset + numOwnedNodes; i++ ){
      // If the node has not been ordered and is within the ownership
      // range of this process, order it now.
      if (reducedNodes[i] < 0){
        reducedNodes[i] = numReducedNodes; 
        numReducedNodes++;
      }
      else {
        reducedNodes[i] = -1;
      }
    }

    if (numReducedNodes > 0){
      // Additive Schwarz ordering should number all locally owned
      // nodes first, and should not require this second ordering.
      if (mat_type == ADDITIVE_SCHWARZ){
        fprintf(stderr, "[%d] Error in additive Schwarz reordering\n",
                mpiRank);
      }

      // Order any remaning variables that are locally owned
      newReducedNodes = new int[ numReducedNodes ];
      computeLocalNodeToNodeCSR(&rowp, &cols, 
                                numReducedNodes, reducedNodes, 
                                noDiagonal);
      computeMatReordering(order_type, numReducedNodes, rowp, cols,
                           NULL, newReducedNodes);

      // Free the allocate CSR data structure
      delete [] rowp;
      delete [] cols;

      // Set the new variable numbers for the boundary nodes
      // and include their offset
      for ( int i = 0, j = 0; i < numNodes; i++ ){
        if (reducedNodes[i] >= 0){
          newNodeNums[i] = offset + newReducedNodes[j];
          j++;
        }
      }

      // Free the new node numbers
      delete [] newReducedNodes;
    }

    delete [] reducedNodes;
  }

  // So now we have new node numbers for the nodes owned by this
  // processor, but the other processors do not have these new numbers
  // yet. Find the values assigned to the nodes requested from
  // external nodes recv_nodes is now an outgoing list of nodes to
  // other processes
  for ( int i = 0; i < recvPtr[mpiSize]; i++ ){
    int node = getLocalNodeNum(recvNodes[i]);
    recvNodes[i] = newNodeNums[node];
  }

  // Now send the new node numbers back to the other processors 
  // that reference them. This also uses all-to-all communication.
  int *newExtNodes = new int[ extPtr[mpiSize] ];
  MPI_Alltoallv(recvNodes, recvCount, recvPtr, MPI_INT,
		newExtNodes, extCount, extPtr, MPI_INT,
		tacs_comm);

  // Once the new node numbers from other processors is received
  // apply these new node numbers back to the locally owned
  // reference numbers.
  for ( int i = 0; i < extNodeOffset; i++ ){
    newNodeNums[i] = newExtNodes[i];
  }
  for ( int i = extNodeOffset; i < numExtNodes; i++ ){
    newNodeNums[i + numOwnedNodes] = newExtNodes[i];
  }

  // Free the new external node numbers
  delete [] newExtNodes;

  // Reorder the local dependent node connectivity
  int *depConn = NULL;
  const int *depPtr = NULL;
  if (depNodes){
    depNodes->getDepNodeReorder(&depPtr, &depConn);
    int end = depPtr[numDependentNodes];
    for ( int i = 0; i < end; i++ ){
      int node = getLocalNodeNum(depConn[i]);
      depConn[i] = newNodeNums[node];
    }
  }

  // Reorder the element connectivity
  int end = elementNodeIndex[numElements];
  for ( int i = 0; i < end; i++ ){
    int node = elementTacsNodes[i];
    if (node >= 0){
      node = getLocalNodeNum(node);
      elementTacsNodes[i] = newNodeNums[node];
    }
  }

  // If boundary conditions are set, reorder them
  if (bcMap){
    int *nodeNums;
    int nbcs = bcMap->getBCNodeNums(&nodeNums);
    for ( int i = 0; i < nbcs; i++ ){
      int node = getLocalNodeNum(nodeNums[i]);
      nodeNums[i] = newNodeNums[node];
    }
  }

  // Finaly, reorder the external node numbers
  for ( int i = 0; i < extNodeOffset; i++ ){
    tacsExtNodeNums[i] = newNodeNums[i];
  }
  for ( int i = extNodeOffset; i < numExtNodes; i++ ){
    tacsExtNodeNums[i] = newNodeNums[i + numOwnedNodes];
  }

  // Resort the external node numbers - these are already unique
  // and the extNodeOffset should not change either
  FElibrary::uniqueSort(tacsExtNodeNums, numExtNodes);

  // Save the mapping to the new numbers for later reorderings
  newNodeIndices = new TACSBVecIndices(&newNodeNums, numNodes);
  newNodeIndices->incref();

  delete [] couplingNodes;
  delete [] extPtr;
  delete [] extCount;
  delete [] recvPtr;
  delete [] recvCount;
  delete [] recvNodes;
}

/*
  Compute the reordering for the given matrix.

  This uses either Reverse Cuthill-McKee (RCM_ORDER), Approximate
  Minimum Degree (AMD) or Nested Disection (ND) to compute a
  reordering of the variables.

  The input to the function is a CSR-type data structure of the
  matrix. Note that for ND (with the external package Metis), requires
  that the diagonal be eliminated from the CSR data structure. (This
  modified data structure can be computed with the no_diagonal flag
  when calling the CSR creation routines.)

  The matrix ordering routines compute a reordering such that:

  P*A*P^{T} has fewer non-zeros.
  
  The function returns an array new_vars such that:

  new_vars = P^{T} old_vars
  perm = P
*/
void TACSAssembler::computeMatReordering( enum OrderingType order_type, 
                                          int nvars, int *rowp, int *cols,
                                          int *perm, int *new_vars ){
  int * _perm = perm;
  int * _new_vars = new_vars;
  if (!perm){ _perm = new int[ nvars ]; }
  if (!new_vars){ _new_vars = new int[ nvars ]; }
  
  if (order_type == RCM_ORDER){
    // Compute the matrix reordering using RCM TACS' version
    // of the RCM algorithm
    int root_node = 0;
    int n_rcm_iters = 1;
    matutils::ComputeRCMOrder(nvars, rowp, cols,
                              _new_vars, root_node, n_rcm_iters);

    if (perm){
      for ( int k = 0; k < nvars; k++ ){
        perm[_new_vars[k]] = k;    
      }
    }
  }
  else if (order_type == AMD_ORDER){
    // Use the approximate minimum degree ordering
    double control[AMD_CONTROL], info[AMD_INFO];
    amd_defaults(control); // Use the default values
    amd_order(nvars, rowp, cols, _perm, 
              control, info);
    
    if (new_vars){
      for ( int k = 0; k < nvars; k++ ){
        new_vars[_perm[k]] = k;
      }
    }
  }
  else if (order_type == ND_ORDER){
    int numflag = 0, options[8] = { 0, 0, 0, 0,  
                                    0, 0, 0, 0 };
    METIS_NodeND(&nvars, rowp, cols, &numflag, 
                 options, _perm, _new_vars);    
  }
  else if (order_type == TACS_AMD_ORDER){
    int use_exact_degree = 0;
    int ncoupling_nodes = 0;
    int * coupling_nodes = NULL;
    amd_order_interface(nvars, rowp, cols, _perm, 
                        coupling_nodes, ncoupling_nodes,
                        use_exact_degree);

    if (new_vars){
      for ( int k = 0; k < nvars; k++ ){
        new_vars[_perm[k]] = k;
      }
    }
  }
  else if (order_type == NATURAL_ORDER){
    if (perm){
      for ( int k = 0; k < nvars; k++ ){
        perm[k] = k;
      }
    }
    if (new_vars){
      for ( int k = 0; k < nvars; k++ ){
        new_vars[k] = k;
      }
    }
  }

  if (!perm){ delete [] _perm; }
  if (!new_vars){ delete [] _new_vars; }
}

/*
  The following function returns a local node number based on the
  provided (global) TACS node number.

  If the node number is on this processor, no search is required,
  however, if the node is externally owned, then a binary search is
  needed to determine the index into the off-processor list of nodes.

  input:
  node:     the global TACS node number unique across all processors

  returns:  the local node number
*/
int TACSAssembler::getLocalNodeNum( int node ){
  // Get the ownership range
  const int *ownerRange;
  varMap->getOwnerRange(&ownerRange); 

  if (node >= ownerRange[mpiRank] &&
      node < ownerRange[mpiRank+1]){
    node = (node - ownerRange[mpiRank]) + extNodeOffset;
  }
  else if (node >= 0){
    const int *ext_nodes = NULL;
    if (tacsExtNodeNums){
      ext_nodes = tacsExtNodeNums;
    }
    else if (extDistIndices){
      extDistIndices->getIndices(&ext_nodes);
    }
    else {
      fprintf(stderr, "[%d] External nodes not defined\n", mpiRank);
      return -1;      
    }

    // Find the local index for external nodes
    int *item = (int*)bsearch(&node, ext_nodes, numExtNodes,
                              sizeof(int), FElibrary::comparator);

    // Check if the item is found in the list
    if (item){
      if (node < ownerRange[mpiRank]){
        node = (item - ext_nodes); 
      }
      else {
        node = numOwnedNodes + (item - ext_nodes); 
      }
    }
    else {
      fprintf(stderr, 
              "[%d] External node %d not in external node list\n",
              mpiRank, node);
      return -1;
    }
  }
  else {
    fprintf(stderr, 
            "[%d] Cannot compute local number for dependent node %d\n",
            mpiRank, node);
    return -1;
  }

  return node;
}

/*
  Given the local node number find the corresponding global TACS node
  number

  This function is the inverse of the getLocalNodeNum() function
  defined above.

  input:
  node:   the local node number

  returns: the global TACS node number
*/
int TACSAssembler::getGlobalNodeNum( int node ){
  // Get the ownership range
  const int *ownerRange;
  varMap->getOwnerRange(&ownerRange); 

  if (node < extNodeOffset){
    const int *ext_nodes = NULL;
    if (tacsExtNodeNums){
      ext_nodes = tacsExtNodeNums;
    }
    else if (extDistIndices){
      extDistIndices->getIndices(&ext_nodes);
    }
    else {
      fprintf(stderr, "[%d] External nodes not defined\n", mpiRank);
      return -1;      
    }

    return ext_nodes[node];
  }
  else if (node < extNodeOffset + numOwnedNodes){
    return (node - extNodeOffset) + ownerRange[mpiRank];
  }
  else if (node < numNodes){
    const int *ext_nodes = NULL;
    if (tacsExtNodeNums){
      ext_nodes = tacsExtNodeNums;
    }
    else if (extDistIndices){
      extDistIndices->getIndices(&ext_nodes);
    }
    else {
      fprintf(stderr, "[%d] External nodes not defined\n", mpiRank);
      return -1;      
    }

    return ext_nodes[node - numOwnedNodes];
  }
  else {
    fprintf(stderr, 
            "[%d] Local node number %d out of range\n",
            mpiRank, node);
    return -1;
  }

  return node;
}

/*!
  The following function creates a data structure that links nodes to
  elements - this reverses the existing data structure that links
  elements to nodes but keeps the original in tact.

  The algorithm proceeds as follows:

  1. The size of the arrays are determined by finding how many nodes
  point to each element

  2. The index into the nodeElem array is determined by adding up the
  contributions from all previous entries.

  3. The original data structure is again traversed and this time an
  element number is associated with each element.
*/
void TACSAssembler::computeNodeToElementCSR( int **_nodeElementPtr,
					     int **_nodeToElements ){
  // Determine the node->element connectivity using local nodes
  int *nodeElementPtr = new int[ numNodes+1 ];
  memset(nodeElementPtr, 0, (numNodes+1)*sizeof(int));

  // Get the dependent node connectivity information
  const int *depNodePtr = NULL;
  const int *depNodeConn = NULL;
  if (depNodes){
    depNodes->getDepNodes(&depNodePtr, &depNodeConn, NULL);
  }
 
  // Loop over all the elements and count up the number of times
  // each node refers to each element
  for ( int i = 0; i < numElements; i++ ){
    int end = elementNodeIndex[i+1];
    for ( int jp = elementNodeIndex[i]; jp < end; jp++ ){
      // Check whether this is locally owned or not
      int node = elementTacsNodes[jp];
 
      if (node >= 0){
        node = getLocalNodeNum(node);
        nodeElementPtr[node+1]++;
      }
      else if (node < 0){
	// This is a dependent-node, determine which independent
	// nodes it depends on
	int dep_node = -node-1;
	int kend = depNodePtr[dep_node+1];
	for ( int kp = depNodePtr[dep_node]; kp < kend; kp++ ){
	  node = getLocalNodeNum(depNodeConn[kp]);
          nodeElementPtr[node+1]++;
        }
      }
    }
  }

  // Sum up the total size of the array
  for ( int i = 0; i < numNodes; i++ ){
    nodeElementPtr[i+1] += nodeElementPtr[i];
  }
  
  // Allocate space for the nodeToElement connectivity
  int size = nodeElementPtr[numNodes];
  int *nodeToElements = new int[ size ];

  // Loop over the elements again, this time adding the nodes to the
  // connectivity
  for ( int i = 0; i < numElements; i++ ){
    int end = elementNodeIndex[i+1];
    for ( int jp = elementNodeIndex[i]; jp < end; jp++ ){
      // Check whether this is locally owned or not
      int node = elementTacsNodes[jp];
      if (node >= 0){
        node = getLocalNodeNum(node);
        nodeToElements[nodeElementPtr[node]] = i;
        nodeElementPtr[node]++;
      }
      else if (node < 0){
	// This is a dependent-node, determine which independent
	// nodes it depends on
	int dep_node = -node-1;
	int kend = depNodePtr[dep_node+1];
	for ( int kp = depNodePtr[dep_node]; kp < kend; kp++ ){
	  node = depNodeConn[kp];
          node = getLocalNodeNum(node);
          nodeToElements[nodeElementPtr[node]] = i;
          nodeElementPtr[node]++;
        }
      }
    }
  }

  // Set up the pointer array which denotes the start (and end) of each node
  for ( int i = 0; i < numNodes; i++ ){
    nodeElementPtr[numNodes-i] = nodeElementPtr[numNodes-i-1];
  }
  nodeElementPtr[0] = 0;

  // Sort and unquify the CSR data structure
  matutils::SortAndUniquifyCSR(numNodes, nodeElementPtr, 
                               nodeToElements, 0);

  // Set the output pointers
  *_nodeToElements = nodeToElements;
  *_nodeElementPtr = nodeElementPtr;
}

/*!
  Set up a CSR data structure pointing from local nodes to other
  local nodes.

  This function works by first estimating the number of entries in
  each row of the matrix. This information is stored temporarily in
  the array rowp. After the contributions from the elements and sparse
  constraints are added, the preceeding elements in rowp are added
  such that rowp becomes the row pointer for the matrix. Note that
  this is an upper bound because the elements and constraints may
  introduce repeated variables. Next, cols is allocated corresponding
  to the column index for each entry. This iterates back over all
  elements and constraints. At this stage, rowp is treated as an array
  of indices, that index into the i-th row of cols[:] where the next
  index should be inserted. As a result, rowp must be adjusted after
  this operation is completed.  The last step is to sort and uniquify
  each row of the matrix.  

  input:
  nodiag:   Remove the diagonal matrix entry 

  output:
  rowp:     the row pointer corresponding to CSR data structure
  colp:     the column indices for each row of the CSR data structure
*/
void TACSAssembler::computeLocalNodeToNodeCSR( int **_rowp, int **_cols, 
					       int nodiag ){
  int *cols = NULL;
  int *rowp = new int[ numNodes+1 ];
  memset(rowp, 0, (numNodes+1)*sizeof(int));

  // Create the node -> element data structure
  int *nodeElementPtr = NULL;
  int *nodeToElements = NULL;
  computeNodeToElementCSR(&nodeElementPtr, &nodeToElements);

  // If we have dependent nodes, use a different algorithm 
  if (depNodes){
    const int *depNodePtr = NULL;
    const int *depNodeConn = NULL;
    depNodes->getDepNodes(&depNodePtr, &depNodeConn, NULL);

    // Count the number of nodes associated with each element
    int *nodeCount = new int[ numElements ];
    memset(nodeCount, 0, numElements*sizeof(int));

    for ( int i = 0; i < numElements; i++ ){
      int jend = elementNodeIndex[i+1];
      for ( int jp = elementNodeIndex[i]; jp < jend; jp++ ){
	int node = elementTacsNodes[jp];
	if (node >= 0){
	  nodeCount[i]++;
	}
	else {
	  // Add the number of independent nodes attached to this
          // dependent node for later use
	  int dep = -node-1;
	  nodeCount[i] += depNodePtr[dep+1] - depNodePtr[dep];
	}
      }
    }

    // First, populate rowp by finding the approximate number of
    // independent nodes per element
    for ( int i = 0; i < numNodes; i++ ){
      for ( int jp = nodeElementPtr[i]; jp < nodeElementPtr[i+1]; jp++ ){
	int elem = nodeToElements[jp];
	rowp[i+1] += nodeCount[elem];
      }
    }

    // Make a conservative estimate of the rowp pointer data
    for ( int i = 0; i < numNodes; i++ ){
      rowp[i+1] += rowp[i];
    }

    // Set up the column indices for each row - label each one with
    // a negative index so that we know what has not been set
    int nnz = rowp[numNodes];
    cols = new int[ nnz ];
    for ( int i = 0; i < nnz; i++ ){
      cols[i] = -1;
    }

    // Add the element contribution to the column indices
    for ( int i = 0; i < numNodes; i++ ){
      for ( int jp = nodeElementPtr[i]; jp < nodeElementPtr[i+1]; jp++ ){
	int elem = nodeToElements[jp];

	// Scan through all the nodes belonging to this element
	int kend = elementNodeIndex[elem+1];
	int row = rowp[i];
	for ( int kp = elementNodeIndex[elem]; kp < kend; kp++ ){
	  int node = elementTacsNodes[kp];
	  if (node >= 0){
            node = getLocalNodeNum(node);
            cols[row] = node;   
            row++;
          }
          else {
            // This is a dependent-node, determine which independent
            // nodes it depends on
            int dep_node = -node-1;
            int pend = depNodePtr[dep_node+1];
            for ( int p = depNodePtr[dep_node]; p < pend; p++ ){
              node = depNodeConn[p];
              node = getLocalNodeNum(node);
              cols[row] = node;   
              row++;
            }
          }
        }

        // Reset the pointer to this row
	rowp[i] = row;
      }
    }

    // Adjust rowp back to a zero-based index
    for ( int i = numNodes; i > 0; i-- ){
      rowp[i] = rowp[i-1];
    }
    rowp[0] = 0;

    delete [] nodeCount;
  }
  else {
    // First, populate rowp by adding the contribution to a node from
    // all adjacent elements.
    for ( int i = 0; i < numNodes; i++ ){
      for ( int j = nodeElementPtr[i]; j < nodeElementPtr[i+1]; j++ ){
	int elem = nodeToElements[j];
	rowp[i+1] += elementNodeIndex[elem+1] - elementNodeIndex[elem];
      }
    }

    // Make a conservative estimate of rowp
    for ( int i = 0; i < numNodes; i++ ){
      rowp[i+1] += rowp[i];
    }

    // Set up the column indices for each row
    int nnz = rowp[numNodes];
    cols = new int[ nnz ];
    for ( int i = 0; i < nnz; i++ ){
      cols[i] = -1;
    }

    // Add the element contribution to the column indices
    for ( int i = 0; i < numNodes; i++ ){
      for ( int jp = nodeElementPtr[i]; jp < nodeElementPtr[i+1]; jp++ ){
	int elem = nodeToElements[jp];
        
        // Add the columns to this row of the sparse matrix
	int row = rowp[i];
        int kend = elementNodeIndex[elem+1];
	for ( int kp = elementNodeIndex[elem]; kp < kend; kp++ ){
          int node = elementTacsNodes[kp];
          node = getLocalNodeNum(node);
          cols[row] = node;   
          row++;
	}
	rowp[i] = row;
      }
    }

    // Adjust rowp back to a zero-based index
    for ( int i = numNodes; i > 0; i-- ){
      rowp[i] = rowp[i-1];
    }
    rowp[0] = 0;
  }

  // Go through and sort/uniquify each row and remove the diagonal if requested
  matutils::SortAndUniquifyCSR(numNodes, rowp, cols, nodiag);

  delete [] nodeElementPtr;
  delete [] nodeToElements;

  *_rowp = rowp;
  *_cols = cols;
}

/*
  Prepare a reduced CSR data structure corresponding to a matrix
  formed from a selection of the global matrix. This routine can be
  used in matrix/variable re-ordering computations.

  This function uses the same algorithm as computeLocalNodeToNodeCSR,
  but performs extra operations required to restrict the computations
  to Ar.  The rnodes array must consist of nrnodes non-negative
  integers between 0 and nrnodes-1, at any arbitrary location. All
  remaining entries of rnodes must be negative.  

  input:
  nrnodes:  the number of reduced nodes
  rnodes:   the indices of the reduced nodes
  nodiag:   flag to indicate whether to remove the diagonal matrix entry

  output:
  rowp:     the row pointer corresponding to CSR data structure
  cols:     the column indices for each row of the CSR data structure
*/
void TACSAssembler::computeLocalNodeToNodeCSR( int **_rowp, int **_cols, 
					       int nrnodes, const int *rnodes,
					       int nodiag ){
  int *cols = NULL;
  int *rowp = new int[ nrnodes+1 ];
  memset(rowp, 0, (nrnodes+1)*sizeof(int));

  // Create/get the node -> element data structure
  int *nodeElementPtr = NULL;
  int *nodeToElements = NULL;
  computeNodeToElementCSR(&nodeElementPtr, &nodeToElements);

  if (depNodes){
    const int *depNodePtr = NULL;
    const int *depNodeConn = NULL;
    depNodes->getDepNodes(&depNodePtr, &depNodeConn, NULL);

    // Count the number of nodes associated with each element
    int *nodeCount = new int[ numElements ];
    memset(nodeCount, 0, numElements*sizeof(int));

    for ( int i = 0; i < numElements; i++ ){
      int jend = elementNodeIndex[i+1];
      for ( int j = elementNodeIndex[i]; j < jend; j++ ){
	int node = elementTacsNodes[j];

	if (node >= 0){
          // Convert to the local node number
          node = getLocalNodeNum(node);
	  if (rnodes[node] >= 0){
	    nodeCount[i]++;
	  }
	}
	else {
	  // Find the dependent node
	  int dep = -node-1;
	  for ( int k = depNodePtr[dep]; k < depNodePtr[dep+1]; k++ ){
            node = getLocalNodeNum(depNodeConn[k]);
	    if (rnodes[node] >= 0){
	      nodeCount[i]++;
	    }
	  }
	}
      }
    }

    // Count up the contribution to the rowp array from all elements
    // using the node->element data
    for ( int i = 0; i < numNodes; i++ ){
      int node = rnodes[i];
      if (node >= 0){
	for ( int j = nodeElementPtr[i]; j < nodeElementPtr[i+1]; j++ ){
	  int elem = nodeToElements[j];
	  rowp[node+1] += nodeCount[elem];
	}
      }
    }

    // Make a conservative estimate of rowp
    for ( int i = 0; i < nrnodes; i++ ){
      rowp[i+1] = rowp[i+1] + rowp[i];
    }

    // Set up the column indices for each row
    int nnz = rowp[nrnodes];
    cols = new int[ nnz ];

    // Add the element contribution to the column indices
    for ( int i = 0; i < numNodes; i++ ){
      int node = rnodes[i];
      if (node >= 0){
	for ( int j = nodeElementPtr[i]; j < nodeElementPtr[i+1]; j++ ){
	  int elem = nodeToElements[j];
	  int kend = elementNodeIndex[elem+1];

	  // Scan through all the nodes belonging to this element
	  int row = rowp[node];
	  for ( int k = elementNodeIndex[elem]; k < kend; k++ ){
	    int local = elementTacsNodes[k]; 
	    if (local >= 0){
              // Get the local node number
              local = getLocalNodeNum(local);
	      int rn = rnodes[local];
              // This is an independent node
              if (rn >= 0){
		cols[row] = rn;  
		row++;
	      }
	    }
	    else {
	      // This is a dependent node, add the dependent node
	      // variables
	      int dep = -local-1;
	      int pend = depNodePtr[dep+1];
	      for ( int p = depNodePtr[dep]; p < pend; p++ ){
                local = depNodeConn[p];
                local = getLocalNodeNum(local);
		int rn = rnodes[local];
		if (rn >= 0){
		  cols[row] = rn; 
		  row++;
		}
	      }
	    }
	  }
	  rowp[node] = row;
	}
      }
    }

    // Adjust rowp back to a zero-based index
    for ( int i = nrnodes; i > 0; i-- ){
      rowp[i] = rowp[i-1];
    }
    rowp[0] = 0;

    delete [] nodeCount;
  }
  else {
    // First, populate rowp by adding the contribution to a node from 
    // all adjacent elements.
    for ( int i = 0; i < numNodes; i++ ){
      int node = rnodes[i];
      if (node >= 0){      
	for ( int j = nodeElementPtr[i]; j < nodeElementPtr[i+1]; j++ ){
	  int elem = nodeToElements[j];
	  // Count up the number of reduced nodes that are required here.
	  int count = 0;
	  for ( int k = elementNodeIndex[elem]; k < elementNodeIndex[elem+1]; k++ ){
            int local = elementTacsNodes[k];
            local = getLocalNodeNum(local);
	    if (rnodes[local] >= 0){
	      count++;
	    }
	  }
	
	  rowp[node+1] += count;
	}
      }
    }

    // Make a conservative estimate of rowp
    for ( int i = 0; i < nrnodes; i++ ){
      rowp[i+1] = rowp[i+1] + rowp[i];
    }

    // Set up the column indices for each row
    int nnz = rowp[nrnodes];
    cols = new int[ nnz ];

    // Add the element contribution to the column indices
    for ( int i = 0; i < numNodes; i++ ){
      int node = rnodes[i];
      if (node >= 0){
	for ( int j = nodeElementPtr[i]; j < nodeElementPtr[i+1]; j++ ){
	  int elem = nodeToElements[j];     
	  int row = rowp[node];
	  for ( int k = elementNodeIndex[elem]; k < elementNodeIndex[elem+1]; k++ ){
            int local = elementTacsNodes[k];
            local = getLocalNodeNum(local);
	    int rn = rnodes[local];
	    if (rn >= 0){
	      cols[row] = rn;
	      row++;
	    }
	  }
	  rowp[node] = row;
	}
      }
    }

    // Adjust rowp back to a zero-based index
    for ( int i = nrnodes; i > 0; i-- ){
      rowp[i] = rowp[i-1];
    }
    rowp[0] = 0;
  }

  // Go through and sort/uniquify each row and remove the diagonal if requested
  matutils::SortAndUniquifyCSR(nrnodes, rowp, cols, nodiag);

  // Free the node -> element data structure
  delete [] nodeToElements;
  delete [] nodeElementPtr;

  *_rowp = rowp;
  *_cols = cols;
}

/*!  
  Compute the local node numbers that correspond to the coupling
  nodes connected to elements on other processes.

  Sort the global node numbers. Match the intervals and send them off
  to the owning process. On the owner, scan through the arrays until
  all the local coupling nodes are found.  

  output:
  couplingNodes:   local node numbers of the coupling nodes

  The following arguments may be NULL inputs:
  extPtr:          pointer into the external node array
  extCount:        external node count
  recvPtr:         incoming external ptr from other processors
  recvCount:       incoming external node count
  recvNodes:       the incoming nodes from other procs
*/
int TACSAssembler::computeCouplingNodes( int **_couplingNodes,
                                         int **_extPtr,
                                         int **_extCount,
                                         int **_recvPtr,
                                         int **_recvCount,
                                         int **_recvNodes ){
  // Get the ownership range and match the intervals of ownership
  const int *ownerRange;
  varMap->getOwnerRange(&ownerRange);

  // Get the external node numbers
  const int *extNodes = tacsExtNodeNums;
  if (extDistIndices){
    extDistIndices->getIndices(&extNodes);
  }

  // Match the intervals for the external node numbers
  int *extPtr = new int[ mpiSize+1 ];
  int *extCount = new int[ mpiSize ];
  FElibrary::matchIntervals(mpiSize, ownerRange, 
                            numExtNodes, extNodes, extPtr);

  // Send the nodes owned by other processors the information
  // First count up how many will go to each process
  for ( int i = 0; i < mpiSize; i++ ){
    extCount[i] = extPtr[i+1] - extPtr[i];
    if (i == mpiRank){ extCount[i] = 0; }
  }

  int *recvCount = new int[ mpiSize ];
  int *recvPtr = new int[ mpiSize+1 ];
  MPI_Alltoall(extCount, 1, MPI_INT, recvCount, 1, MPI_INT, tacs_comm);

  // Now, send the node numbers to the other processors
  recvPtr[0] = 0;
  for ( int i = 0; i < mpiSize; i++ ){
    recvPtr[i+1] = recvPtr[i] + recvCount[i];
  }

  // Number of nodes that will be received from other procs
  int *recvNodes = new int[ recvPtr[mpiSize] ];
  MPI_Alltoallv((void*)extNodes, extCount, extPtr, MPI_INT, 
		recvNodes, recvCount, recvPtr, MPI_INT, tacs_comm);

  // Sort the recv'd nodes
  int *recvNodesSorted = NULL;
  if (_recvNodes){ 
    recvNodesSorted = new int[ recvPtr[mpiSize] ];
    memcpy(recvNodesSorted, recvNodes, recvPtr[mpiSize]*sizeof(int));
  }
  else {
    recvNodesSorted = recvNodes;
  }

  // Uniquely sort the recieved nodes
  int nextern_unique = 
    FElibrary::uniqueSort(recvNodesSorted, recvPtr[mpiSize]);

  // Count up the number of coupling nodes
  int numCouplingNodes = nextern_unique + numExtNodes;
  if (_couplingNodes){
    int *couplingNodes = new int[ numCouplingNodes ];

    // Automatically add in the external node numbers
    int index = 0;
    for ( int i = 0; i < extNodeOffset; i++, index++ ){
      couplingNodes[index] = i;
    }
    
    // Add the coupling nodes received from other processors
    for ( int i = 0; i < nextern_unique; i++, index++ ){
      couplingNodes[index] = getLocalNodeNum(recvNodesSorted[i]);
    }
    
    // add in the remaining external nodes
    for ( int i = extNodeOffset; i < numExtNodes; i++, index++ ){
      couplingNodes[index] = numOwnedNodes + i;
    }

    *_couplingNodes = couplingNodes;
  }

  if (_extPtr){ *_extPtr = extPtr; }
  else { delete [] extPtr; }
  if (_extCount){ *_extCount = extCount; }
  else { delete [] extCount; }
  if (_recvPtr){ *_recvPtr = recvPtr; }
  else { delete [] recvPtr; }
  if (_recvCount){ *_recvCount = recvCount; }
  else { delete [] recvCount; }
  if (_recvNodes){ 
    *_recvNodes = recvNodes; 
    delete [] recvNodesSorted;
  }
  else { delete [] recvNodes; }

  return numCouplingNodes;
}

/*!
  Compute the elements that couple with other processors.
  
  Compute the coupling nodes and the node to element pointer
  CSR data structure. From these, collect all elements that "own" a
  node that is referred to from another process.  
*/
int TACSAssembler::computeCouplingElements( int **_couplingElems ){
  // Compute the nodes that couple to other processors
  int *couplingNodes;
  int numCouplingNodes = computeCouplingNodes(&couplingNodes);

  // Compute the node->element data structure
  int *nodeElementPtr, *nodeToElements;
  computeNodeToElementCSR(&nodeElementPtr, &nodeToElements);

  // Determine the elements that contain a coupling node
  int numCouplingElems = 0;
  int *couplingElems = new int[ numElements ];

  // Loop over all the coupling nodes and add all the elements
  // touched by each coupling node
  for ( int i = 0; i < numCouplingNodes; i++ ){
    int cnode = couplingNodes[i];

    for ( int j = nodeElementPtr[cnode]; j < nodeElementPtr[cnode+1]; j++ ){
      int elem = nodeToElements[j];
      numCouplingElems = 
        FElibrary::mergeArrays(couplingElems, numCouplingElems, &elem, 1);
    }
  }

  // Free the data
  delete [] nodeElementPtr;
  delete [] nodeToElements;
  delete [] couplingNodes;

  *_couplingElems = couplingElems;
  return numCouplingElems;
}

/*!  
  The function initialize performs a number of synchronization
  tasks that prepare the finite-element model for use.

  tacsNodeNums[i] is the global node number for the local node number i

  Two objects are required:
  1. VarMap is constructed with the block sizes of each 
  node owned by this process

  2. VecDistribute is constructed so that it takes an array and
  distributes its values to a vector or takes the vector values and
  collects them into an array This requires a sorted array of global
  node numbers.  
*/
int TACSAssembler::initialize(){
  if (meshInitializedFlag){
    fprintf(stderr, "[%d] Cannot call initialize() more than once!\n", 
	    mpiRank);
    return 1;
  }
  if (numDependentNodes > 0 && !depNodes){
    fprintf(stderr, "[%d] Error: Dependent nodes not defined\n",
	    mpiRank);
    return 1;
  }
  if (!elements){
    fprintf(stderr, "[%d] Error: Elements not defined\n",
	    mpiRank);
    return 1;
  }
  if (!elementNodeIndex){
    fprintf(stderr, "[%d] Error: Element connectivity not defined\n",
	    mpiRank);
    return 1;
  }
  
  // If the external nodes have not been computed, compute them now...
  if (!tacsExtNodeNums){
    computeExtNodes();
  }

  // Flag to indicate that we've initialized TACSAssembler -
  // the initialization can only be done once
  meshInitializedFlag = 1;

  // Set up data for any dependent nodes. Note that the minimum
  // number of independent nodes is set as the maximum number
  // of element node by default. This is required for addMatValues()
  // to have enough memory for TRANSPOSE matrix assembly.
  maxElementIndepNodes = maxElementNodes;
  if (numDependentNodes > 0){
    const int *depNodePtr;
    depNodes->getDepNodes(&depNodePtr, NULL, NULL);

    // Compute the maximum number of independent nodes
    for ( int i = 0; i < numElements; i++ ){
      int jend = elementNodeIndex[i+1];
      int nnodes = 0;
      for ( int j = elementNodeIndex[i]; j < jend; j++ ){
	if (elementTacsNodes[j] >= 0){
	  nnodes++;
	}
	else {
	  int dep = -elementTacsNodes[j]-1;
	  nnodes += depNodePtr[dep+1] - depNodePtr[dep];
	}
      }
      if (nnodes > maxElementIndepNodes){
	maxElementIndepNodes = nnodes;
      }
    }
  }

  // Create the distribution between the local nodes and the global ones
  extDistIndices = new TACSBVecIndices(&tacsExtNodeNums,
                                       numExtNodes);
  tacsExtNodeNums = NULL;
  extDistIndices->incref();
  extDistIndices->setUpInverse();
  
  // Set up the external indices
  extDist = new TACSBVecDistribute(varMap, extDistIndices);
  extDist->incref();

  // Scatter the boundary conditions to the external nodes
  scatterExternalBCs();

  // Allocate the vectors
  varsVec = createVec();  varsVec->incref();
  dvarsVec = createVec();  dvarsVec->incref();
  ddvarsVec = createVec();  ddvarsVec->incref();
  xptVec = createNodeVec();  xptVec->incref();

  // Allocate memory for the working array:
  // Determine the size of the data working array
  // max requirement is 4 element variable-size arrays,
  // 2 node-size arrays and either the element matrix or
  // the derivative of the residuals w.r.t. the nodes.
  int dataSize = maxElementIndepNodes + 4*maxElementSize + 
    2*TACS_SPATIAL_DIM*maxElementNodes;
  if (TACS_SPATIAL_DIM*maxElementNodes > maxElementSize){
    dataSize += TACS_SPATIAL_DIM*maxElementNodes*maxElementSize; 
  }
  else {
    dataSize += maxElementSize*maxElementSize;
  }
  elementData = new TacsScalar[ dataSize ];

  int idataSize = maxElementIndepNodes + maxElementNodes+1;
  elementIData = new int[ idataSize ];
}

/*
  Scatter the boundary conditions that are shared between processors

  Note that we do not need to scatter the values along with the
  boundary condition values along with the boundary condition
  variables because the values are only used on the processors which
  own the nodes (that already have the correct information.)
  
  This function is called during initialize()
*/
void TACSAssembler::scatterExternalBCs(){
  // Get the coupling nodes shared between processors
  int *extPtr, *extCount;
  int *recvPtr, *recvCount, *recvNodes;
  int numCoupling = 
    computeCouplingNodes(NULL, &extPtr, &extCount,
                         &recvPtr, &recvCount, &recvNodes);
  
  // Get the nodes/variables
  const int *nodes, *vars;
  int nbcs = bcMap->getBCs(&nodes, &vars, NULL);

  // Allocate the maximum array size that will be required
  int max_recv_size = 100;
  int *recvBCs = new int[ max_recv_size ];
  int index = 0;
  
  // Search through the coupling nodes recvd from other procs
  recvPtr[0] = 0;
  int ptr = 0;
  for ( int k = 0; k < mpiSize; k++ ){
    // Utilize the fact that the recvNodes are sorted
    // from each processor
    if (recvCount[k] > 0){
      int size = recvCount[k];
      for ( int i = 0; i < nbcs; i++ ){
        int *item = (int*)bsearch(&nodes[i], &recvNodes[ptr], size,
                                  sizeof(int), FElibrary::comparator);

        // This node is an interface node and a boundary node
        // add it to the list
        if (item){
          // Extend the array if required
          if (2*(index+1) >= max_recv_size){
            max_recv_size *= 2;
            int *temp = new int[ max_recv_size ];
            memcpy(temp, recvBCs, 2*index*sizeof(int));
            delete [] recvBCs;
            recvBCs = temp;
          }

          // Set the new values into the array
          recvBCs[2*index] = nodes[i];
          recvBCs[2*index+1] = vars[i];
          index++;
        }
      }

      // Update the pointer into the BC array
      ptr += size;
    }

    // Record the number of newly added nodes
    recvPtr[k+1] = 2*index;
    recvCount[k] = 2*index - recvPtr[k];
  }

  // Send the counts to the other procs
  MPI_Alltoall(recvCount, 1, MPI_INT, 
               extCount, 1, MPI_INT, tacs_comm);

  // Count up the size
  extPtr[0] = 0;
  for ( int i = 0; i < mpiSize; i++ ){
    extPtr[i+1] = extPtr[i] + extCount[i];
  }

  // Allocate an array for the incoming data
  int numExtBCs = extPtr[mpiSize];
  int *extBCs = new int[ numExtBCs ];
  MPI_Alltoallv(recvBCs, recvCount, recvPtr, MPI_INT,
                extBCs, extCount, extPtr, MPI_INT, tacs_comm);

  // Free the data that is no longer required
  delete [] recvBCs;
  delete [] extPtr;
  delete [] extCount;
  delete [] recvPtr;
  delete [] recvCount;
  delete [] recvNodes;

  // Add the external boundary conditions
  for ( int k = 0; k < numExtBCs; k += 2 ){
    bcMap->addBinaryFlagBC(extBCs[k], extBCs[k+1]);
  }
  delete [] extBCs;
}

/*
  Get pointers to the element data. This code provides a way to
  automatically segment an array to avoid coding mistakes.

  Note that this is coded in such a way that you can provide NULL
  arguments to
*/
void TACSAssembler::getDataPointers( TacsScalar *data,
				     TacsScalar **v1, TacsScalar **v2, 
				     TacsScalar **v3, TacsScalar **v4,
				     TacsScalar **x1, TacsScalar **x2, 
				     TacsScalar **weights,
				     TacsScalar **mat ){
  int s = 0;
  if (v1){ *v1 = &data[s];  s += maxElementSize; }
  if (v2){ *v2 = &data[s];  s += maxElementSize; }
  if (v3){ *v3 = &data[s];  s += maxElementSize; }
  if (v4){ *v4 = &data[s];  s += maxElementSize; }
  if (x1){ *x1 = &data[s];  s += TACS_SPATIAL_DIM*maxElementNodes; };
  if (x2){ *x2 = &data[s];  s += TACS_SPATIAL_DIM*maxElementNodes; };
  if (weights){ *weights = &data[s];  s += maxElementIndepNodes; }
  if (mat){
    *mat = &data[s];
  }
}

/*
  Get the ordering from the old nodes to the new nodes

  input/output:
  oldToNew:  array of size equal to the number of owned nodes
*/
void TACSAssembler::getReordering( int *oldToNew ){
  if (newNodeIndices){
    // Get the new node indices
    const int *newNodes;
    newNodeIndices->getIndices(&newNodes);
    memcpy(oldToNew, &newNodes[extNodeOffset], numOwnedNodes*sizeof(int));
  }
  else {
    const int *ownerRange;
    varMap->getOwnerRange(&ownerRange);
    for ( int k = 0; k < numOwnedNodes; k++ ){
      oldToNew[k] = ownerRange[mpiRank] + k;
    }
  }
}

/*
  Reorder the vector using the reordering computed using the
  computeReordering() call.

  This is useful for reordering nodal vectors after the reordering has
  been applied. 

  input/output:
  vec:    the vector to be reordered in place
*/
void TACSAssembler::reorderVec( TACSBVec *vec ){
  if (newNodeIndices){
    // Get the ownership range
    const int *ownerRange;
    varMap->getOwnerRange(&ownerRange);

    // Get the vector of values from the array
    TacsScalar *x;
    int bsize = vec->getBlockSize();
    int size = vec->getArray(&x);

    // Allocate an array to store the old values and fill them in
    TacsScalar *xold = new TacsScalar[ size ];
    memcpy(xold, x, size*sizeof(TacsScalar));

    // Get the new node indices
    const int *newNodes;
    newNodeIndices->getIndices(&newNodes);

    // Loop through the owned nodes
    for ( int i = 0; i < numOwnedNodes; i++ ){
      // Get the new node value
      int node = newNodes[extNodeOffset+i];
      node = node - ownerRange[mpiRank];

      // Copy the values back to the array in the new
      // order
      for ( int k = 0; k < bsize; k++ ){
        x[bsize*node + k] = xold[bsize*i + k];
      }
    }

    delete [] xold;
  }
}

/*!
  Collect all the design variable values assigned by this process

  This code does not ensure consistency of the design variable values
  between processes. If the values of the design variables are
  inconsistent to begin with, the maximum design variable value is
  returned. Call setDesignVars to make them consistent.

  Each process contains objects that maintain their own design
  variable values. Ensuring the consistency of the ordering is up to
  the user. Having multiply-defined design variable numbers
  corresponding to different design variables results in undefined
  behaviour.

  dvs:    the array of design variable values (output)
  numDVs: the number of design variables
*/
void TACSAssembler::getDesignVars( TacsScalar dvs[], int numDVs ){
  TacsScalar * tempDVs = new TacsScalar[ numDVs ];
  memset(tempDVs, 0, numDVs*sizeof(TacsScalar));

  // Get the design variables from the elements on this process 
  for ( int i = 0; i < numElements; i++ ){
    elements[i]->getDesignVars(tempDVs, numDVs);
  }
  
  // Get the design variables from the auxiliary elements
  if (auxElements){
    auxElements->getDesignVars(tempDVs, numDVs);
  }

  MPI_Allreduce(tempDVs, dvs, numDVs, TACS_MPI_TYPE, 
		TACS_MPI_MAX, tacs_comm);
  
  // Free the allocated array
  delete [] tempDVs;
}

/*!
  Set the design variables.

  The design variable values provided must be the same on all
  processes for consistency. This call however, is not collective.

  dvs:    the array of design variable values
  numDVs: the number of design variables
*/
void TACSAssembler::setDesignVars( const TacsScalar dvs[], int numDVs ){
  for ( int i = 0; i < numElements; i++ ){
    elements[i]->setDesignVars(dvs, numDVs);
  }

  // Set the design variables in the auxiliary elements
  if (auxElements){
    auxElements->setDesignVars(dvs, numDVs);
  }
}

/*
  Retrieve the design variable range.

  This call is collective on all TACS processes. The ranges provided
  by indivdual objects may not be consistent (if someone provided
  incorrect data they could be.) Make a best guess; take the minimum
  upper bound and the maximum lower bound.

  lowerBound: the lower bound on the design variables (output)
  upperBound: the upper bound on the design variables (output)
  numDVs:     the number of design variables
*/
void TACSAssembler::getDesignVarRange( TacsScalar lb[],
				       TacsScalar ub[],
				       int numDVs ){
  // Get the design variables from the elements on this process 
  for ( int i = 0; i < numElements; i++ ){
    elements[i]->getDesignVarRange(lb, ub, numDVs);
  }

  // Get the design variable range from the auxiliary elements
  if (auxElements){
    auxElements->getDesignVarRange(lb, ub, numDVs);
  }
}			  
/*!
  Set the number of threads to use in the computation
*/
void TACSAssembler::setNumThreads( int t ){
  thread_info->setNumThreads(t);  
}

/*
  Create a distributed vector.
  
  Vector classes initialized by one TACS object, cannot be used by a
  second, unless they share are exactly the parallel layout.
*/
TACSBVec *TACSAssembler::createVec(){
  if (!meshInitializedFlag){
    fprintf(stderr, "[%d] Cannot call createVec() before initialize()\n", 
	    mpiRank);
    return NULL;
  }

  // Create the vector with all the bells and whistles 
  return new TACSBVec(varMap, varsPerNode, 
                      bcMap, extDist, depNodes);
}

/*!
  Create a distributed matrix

  This matrix is distributed in block-rows. Each processor owns a
  local part of the matrix and an off-diagonal part which couples
  between processors.
 
  This code creates a local array of global indices that is used to
  determine the destination for each entry in the sparse matrix.  This
  TACSBVecIndices object is reused if any subsequent DistMat objects
  are created.
*/
DistMat *TACSAssembler::createMat(){
  if (!meshInitializedFlag){
    fprintf(stderr, "[%d] Cannot call createMat() before initialize()\n", 
	    mpiRank);
    return NULL;
  }
  
  // Create the distMat indices if they do not already exist
  if (!distMatIndices){
    // Get the global node numbering
    int *indices = new int[ numNodes ];
    for ( int i = 0; i < numNodes; i++ ){
      indices[i] = getGlobalNodeNum(i);
    }

    distMatIndices = new TACSBVecIndices(&indices, numNodes);
    distMatIndices->incref();
    distMatIndices->setUpInverse();
  }

  // Compute the local connectivity
  int *rowp, *cols;
  computeLocalNodeToNodeCSR(&rowp, &cols);
  
  // Create the distributed matrix class
  DistMat *dmat = new DistMat(thread_info, varMap, varsPerNode,
                              numNodes, rowp, cols,
                              distMatIndices, bcMap);

  // Free the local connectivity
  delete [] rowp;
  delete [] cols;

  // Return the resulting matrix object
  return dmat;
}

/*!  
  Create a parallel matrix for finite-element analysis.
  
  On the first call, this computes a reordering with the scheme
  provided. On subsequent calls, the reordering scheme is reused so
  that all FEMats, created from the same TACSAssembler object have
  the same non-zero structure. This makes adding matrices together
  easier (which is required for eigenvalue computations.)

  The first step is to determine the coupling nodes. (For a serial
  case there are no coupling nodes, so this is very simple!)  Then,
  the nodes that are not coupled to other processes are determined.
  The coupling and non-coupling nodes are ordered separately.  The
  coupling nodes must be ordered at the end of the block, while the
  local nodes must be ordered first. This type of constraint is not
  usually imposed in matrix ordering routines, so here we use a
  kludge.  First, order all the nodes and determine the ordering of
  the coupling variables within the full set. Next, order the local
  nodes. This hopefully reduces the fill-ins required, although there
  is no firm proof to back that up.

  The results from the reordering are placed in a set of objects.  The
  matrix reordering is stored in feMatBIndices and feMatCIndices while
  two mapping objects are created that map the variables from the
  global vector to reordered matrix.

  Mathematically this reordering can be written as follows,

  A' = (P A P^{T})

  where P^{T} is a permutation of the columns (variables), while P is
  a permutation of the rows (equations).
*/
FEMat *TACSAssembler::createFEMat( enum OrderingType order_type ){
  if (!meshInitializedFlag){
    fprintf(stderr, "[%d] Cannot call createFEMat() before initialize()\n", 
	    mpiRank);
    return NULL;
  }
  if (order_type == NATURAL_ORDER){
    fprintf(stderr, 
	    "[%d] Cannot call createFEMat() with \
order_type == NATURAL_ORDER\n",
	    mpiRank);
    order_type = TACS_AMD_ORDER;
  }

  if (!feMatBMap){   
    // The number of local nodes and the number of coupling nodes
    // that are referenced by other processors
    int nlocal_nodes = numNodes;
    int ncoupling_nodes = 0;
    
    // The local nodes and their
    int *perm_local_nodes = NULL;
    int *tacs_local_nodes = NULL;
    int *perm_coupling_nodes = NULL;
    int *tacs_coupling_nodes = NULL;

    if (order_type == TACS_AMD_ORDER){
      // Use the AMD ordering scheme in TACS to compute an ordering of
      // the nodes that reduces the fill-in in the complete matrix --
      // including the off-diagonal contributions. This can reduce the
      // computational effort and the discrepancy between factorization
      // times on different processes.

      // Find the local node numbers of the coupling nodes.
      // Note that this is a sorted list
      int *coupling_nodes;
      ncoupling_nodes = computeCouplingNodes(&coupling_nodes);
      nlocal_nodes = numNodes - ncoupling_nodes;
      
      // Compute the CSR data structure for the node-to-node
      // connectivity without the diagonal entry
      int *rowp, *cols;
      int no_diagonal = 1;
      computeLocalNodeToNodeCSR(&rowp, &cols, no_diagonal);
      
      // Here perm is the entire permutation array
      int *perm = new int[ numNodes ];
      int use_exact_degree = 0; // Don't use the exact degree
      amd_order_interface(numNodes, rowp, cols, perm, 
                          coupling_nodes, ncoupling_nodes, use_exact_degree);
      
      // Free the rowp/cols array (which are modified by the
      // reordering anyway)
      delete [] rowp;
      delete [] cols; 

      // Compute the local node permutation and the new
      perm_local_nodes = new int[ nlocal_nodes ];
      tacs_local_nodes = new int[ nlocal_nodes ];
      for ( int i = 0; i < nlocal_nodes; i++ ){
        perm_local_nodes[i] = perm[i];
        tacs_local_nodes[i] = getGlobalNodeNum(perm_local_nodes[i]);
      }

      perm_coupling_nodes = new int[ ncoupling_nodes ];
      tacs_coupling_nodes = new int[ ncoupling_nodes ];
      for ( int i = 0; i < ncoupling_nodes; i++ ){
        perm_coupling_nodes[i] = perm[i + nlocal_nodes];
        tacs_coupling_nodes[i] = getGlobalNodeNum(perm_coupling_nodes[i]);
      }

      delete [] perm;
      delete [] coupling_nodes;
    }
    else {
      // This scheme uses AMD or Nested Disection on the local and
      // coupling nodes independently. This ignores the off-diagonal
      // fill-ins which can be considerable!
      int no_diagonal = 0;
      if (order_type == ND_ORDER){
        no_diagonal = 1;
      }

      // Find the local node numbers of the coupling nodes.
      // Note that this is a sorted list
      int * coupling_nodes;
      ncoupling_nodes = computeCouplingNodes(&coupling_nodes);
      nlocal_nodes = numNodes - ncoupling_nodes;
      
      // Set the coupling nodes for ordering
      int * all_nodes = new int[ numNodes ];
      for ( int k = 0; k < numNodes; k++ ){
        all_nodes[k] = -1;
      }
      
      for ( int k = 0; k < ncoupling_nodes; k++ ){
        all_nodes[coupling_nodes[k]] = k;
      }
      
      perm_coupling_nodes = new int[ ncoupling_nodes ];    
      tacs_coupling_nodes = new int[ ncoupling_nodes ];
      
      // Now, compute the reordering for the local coupling variables
      if (ncoupling_nodes > 0){
        int *rowp, *cols;
        computeLocalNodeToNodeCSR(&rowp, &cols, 
                                  ncoupling_nodes, all_nodes,
                                  no_diagonal);
        
        // Compute the permutation of the coupling nodes
        computeMatReordering(order_type, ncoupling_nodes, rowp, cols, 
                             perm_coupling_nodes, NULL);
        
        for ( int i = 0; i < ncoupling_nodes; i++ ){
          // Permute the coupling_nodes array - store in perm_coupling_nodes
          perm_coupling_nodes[i] = coupling_nodes[perm_coupling_nodes[i]];
          tacs_coupling_nodes[i] = getGlobalNodeNum(perm_coupling_nodes[i]);
        }
        
        delete [] rowp;
        delete [] cols;
      }
      
      // Set the remaining, local nodes for coupling
      perm_local_nodes = new int[ nlocal_nodes ];
      tacs_local_nodes = new int[ nlocal_nodes ];
      int *local_nodes = new int[ nlocal_nodes ];
      for ( int j = 0, k = 0; k < numNodes; k++ ){
        if (all_nodes[k] < 0){
          all_nodes[k] = j;
          local_nodes[j] = k;
          j++;
        }
        else {
          all_nodes[k] = -1;
        }
      }

      // Now, compute the reordering for the local variables
      int *rowp, *cols;    
      computeLocalNodeToNodeCSR(&rowp, &cols, 
                                nlocal_nodes, all_nodes,
                                no_diagonal);
      computeMatReordering(order_type, nlocal_nodes, rowp, cols,
                           perm_local_nodes, NULL);
      
      for ( int i = 0; i < nlocal_nodes; i++ ){
        // Permute the local nodes and record the corresponding tacs variables
        perm_local_nodes[i] = local_nodes[perm_local_nodes[i]];
        tacs_local_nodes[i] = getGlobalNodeNum(perm_local_nodes[i]);
      }
      
      delete [] rowp;
      delete [] cols;
      delete [] coupling_nodes;
      delete [] all_nodes;
      delete [] local_nodes;    
    }

    // Create persistent objects so that all further FEMats will have
    // the same ordering.
    feMatBIndices = new TACSBVecIndices(&perm_local_nodes, nlocal_nodes);
    feMatCIndices = new TACSBVecIndices(&perm_coupling_nodes, ncoupling_nodes);
    feMatBIndices->incref();
    feMatCIndices->incref();

    TACSBVecIndices *tlocal = new TACSBVecIndices(&tacs_local_nodes, nlocal_nodes);
    TACSBVecIndices *tcoupling = new TACSBVecIndices(&tacs_coupling_nodes, 
                                                     ncoupling_nodes);
    feMatBMap = new TACSBVecDistribute(varMap, tlocal);
    feMatCMap = new TACSBVecDistribute(varMap, tcoupling);
    feMatBMap->incref();
    feMatCMap->incref();
  }

  // Compute he local non-zero pattern
  int *rowp, *cols;
  computeLocalNodeToNodeCSR(&rowp, &cols);

  FEMat *fmat = new FEMat(thread_info, varMap, 
                          varsPerNode, numNodes, rowp, cols,
                          feMatBIndices, feMatBMap,
                          feMatCIndices, feMatCMap, bcMap);
  delete [] rowp;
  delete [] cols;

  return fmat;
}

/*
  Retrieve the initial conditions associated with the problem
*/
void TACSAssembler::getInitConditions( TACSBVec *vars, TACSBVec *dvars ){
  // Retrieve pointers to temporary storage
  TacsScalar *elemVars, *elemDVars, *elemXpts;
  getDataPointers(elementData, &elemVars, &elemDVars, NULL, NULL,
		  &elemXpts, NULL, NULL, NULL);

  // Retrieve the initial condition values from each element
  for ( int i = 0; i < numElements; i++ ){
    int ptr = elementNodeIndex[i];
    int len = elementNodeIndex[i+1] - ptr;
    const int *nodes = &elementTacsNodes[ptr];
    xptVec->getValues(len, nodes, elemXpts);

    // Get the initial condition values
    elements[i]->getInitCondition(elemVars, elemDVars, elemXpts);

    // Set the values into the vectors
    vars->setValues(len, nodes, elemVars, INSERT_VALUES);
    dvars->setValues(len, nodes, elemDVars, INSERT_VALUES);
  }

  vars->beginSetValues(INSERT_VALUES);
  dvars->beginSetValues(INSERT_VALUES);
  vars->endSetValues(INSERT_VALUES);
  dvars->endSetValues(INSERT_VALUES);
}

/*
  Zero the entries of the local variables
*/
void TACSAssembler::zeroVariables(){
  varsVec->zeroEntries();
}

/*
  Zero the values of the time-derivatives of the state variables.
  This time-derivative is load-case independent.
*/
void TACSAssembler::zeroDotVariables(){
  dvarsVec->zeroEntries();
}

/*
  Zero the values of the time-derivatives of the state variables.
  This time-derivative is load-case independent.
*/
void TACSAssembler::zeroDDotVariables(){
  ddvarsVec->zeroEntries();
}

/*
  Set the value of the time/variables/time derivatives simultaneously
*/
void TACSAssembler::setVariables( TACSBVec *q, 
                                  TACSBVec *qdot, 
                                  TACSBVec *qddot ){
  // Copy the values to the array. Only local values are 
  // copied, not external/dependents
  if (q){ varsVec->copyValues(q); }
  if (qdot){ dvarsVec->copyValues(qdot); }
  if (qddot){ ddvarsVec->copyValues(qddot); }

  // Distribute the values
  if (q){ varsVec->beginDistributeValues(); }
  if (qdot){ dvarsVec->beginDistributeValues(); }
  if (qddot){ ddvarsVec->beginDistributeValues(); }
  if (q){ varsVec->endDistributeValues(); }
  if (qdot){ dvarsVec->endDistributeValues(); }
  if (qddot){ ddvarsVec->endDistributeValues(); }
}

/*
  Get the variables from the vectors in TACS
*/
void TACSAssembler::getVariables( TACSBVec *q, 
                                  TACSBVec *qdot, 
                                  TACSBVec *qddot ){
  // Copy the values to the array. Only local values are 
  // copied, not external/dependents
  if (q){ q->copyValues(varsVec); }
  if (qdot){ qdot->copyValues(dvarsVec); }
  if (qddot){ qddot->copyValues(ddvarsVec); }}

/*
  Set the simulation time internally in the TACSAssembler object
*/
void TACSAssembler::setSimulationTime( double _time ){
  time = _time;
}

/*
  Retrieve the simulation time from the TACSAssembler object
*/
double TACSAssembler::getSimulationTime(){
  return time;
}

/*
  Evaluates the total kinetic and potential energies of the structure
*/
void TACSAssembler::evalEnergies( TacsScalar *Te, TacsScalar *Pe ){
  // Zero the kinetic and potential energy
  *Te = 0.0;
  *Pe = 0.0;

  // Array for storing local kinetic and potential energies
  TacsScalar elem_energies[2] = {0.0, 0.0};
 
  // Retrieve pointers to temporary storage
  TacsScalar *vars, *dvars, *elemXpts;
  getDataPointers(elementData, &vars, &dvars, 
                  NULL, NULL, &elemXpts, NULL, NULL, NULL);
  
  // Loop over all elements and add individual contributions to the
  // total energy
  for ( int i = 0; i < numElements; i++ ){
    int ptr = elementNodeIndex[i];
    int len = elementNodeIndex[i+1] - ptr;
    const int *nodes = &elementTacsNodes[ptr];
    xptVec->getValues(len, nodes, elemXpts);
    varsVec->getValues(len, nodes, vars);
    dvarsVec->getValues(len, nodes, dvars);
    
    // Compute and add the element's contributions to the total
    // energy
    TacsScalar elemTe, elemPe;
    elements[i]->computeEnergies(time, &elemTe, &elemPe,
                                 elemXpts, vars, dvars);
    
    // Add up the kinetic and potential energy
    *Te += elemTe;
    *Pe += elemPe;
  }
  
  // Sum up the kinetic and potential energies across all processors
  TacsScalar input[2], output[2];
  input[0] = *Te;
  input[1] = *Pe;    
  MPI_Allreduce(input, output, 2, TACS_MPI_TYPE, 
                MPI_SUM, tacs_comm);
  
  *Te = output[0];
  *Pe = output[1]; 
} 

/*!  
  Assemble the residual associated with the input load case.  
  
  This residual includes the contributions from element tractions set
  in the TACSSurfaceTraction class and any point loads. Note that the
  vector entries are zeroed first, and that the Dirichlet boundary
  conditions are applied after the assembly of the residual is
  complete.
  
  rhs:      the residual output
*/
void TACSAssembler::assembleRes( TACSBVec *residual ){
  // Sort the list of auxiliary elements - this only performs the
  // sort if it is required (if new elements are added)
  if (auxElements){
    auxElements->sort();
  }

  // Zero the residual
  residual->zeroEntries();

  if (thread_info->getNumThreads() > 1){
    // Set the number of completed elements to zero
    numCompletedElements = 0;
    tacsPInfo->tacs = this;

    // Create the joinable attribute
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    for ( int k = 0; k < thread_info->getNumThreads(); k++ ){
      pthread_create(&threads[k], &attr, 
		     TACSAssembler::assembleRes_thread,
                     (void*)tacsPInfo);
    }

    // Join all the threads
    for ( int k = 0; k < thread_info->getNumThreads(); k++ ){
      pthread_join(threads[k], NULL);
    }

    // Destroy the attribute
    pthread_attr_destroy(&attr); 
  }
  else {
    // Retrieve pointers to temporary storage
    TacsScalar *vars, *dvars, *ddvars, *elemRes, *elemXpts;
    getDataPointers(elementData, 
		    &vars, &dvars, &ddvars, &elemRes,
		    &elemXpts, NULL, NULL, NULL);
    
    // Get the auxiliary elements
    int naux = 0, aux_count = 0;
    TACSAuxElem *aux = NULL;
    if (auxElements){
      naux = auxElements->getAuxElements(&aux);
    }

    // Go through and add the residuals from all the elements
    for ( int i = 0; i < numElements; i++ ){
      int ptr = elementNodeIndex[i];
      int len = elementNodeIndex[i+1] - ptr;
      const int *nodes = &elementTacsNodes[ptr];
      xptVec->getValues(len, nodes, elemXpts);
      varsVec->getValues(len, nodes, vars);
      dvarsVec->getValues(len, nodes, dvars);
      ddvarsVec->getValues(len, nodes, ddvars);

      // Add the residual from the working element
      int nvars = elements[i]->numVariables();
      memset(elemRes, 0, nvars*sizeof(TacsScalar));
      elements[i]->addResidual(time, elemRes, elemXpts, 
                               vars, dvars, ddvars);
      
      // Add the residual from any auxiliary elements
      while (aux_count < naux && aux[aux_count].num == i){
        aux[aux_count].elem->addResidual(time, elemRes, elemXpts,
                                         vars, dvars, ddvars);
        aux_count++;
      }

      // Add the residual values
      residual->setValues(len, nodes, elemRes, ADD_VALUES);
    }
  }

  // Finish transmitting the residual
  residual->beginSetValues(ADD_VALUES);
  residual->endSetValues(ADD_VALUES);

  // Apply the boundary conditions for the residual
  residual->applyBCs(varsVec);
}
  
/*!
  Assemble the Jacobian matrix

  This function assembles the global Jacobian matrix and
  residual. This Jacobian includes the contributions from all
  elements. The Dirichlet boundary conditions are applied to the
  matrix by zeroing the rows of the matrix associated with a boundary
  condition, and setting the diagonal to unity. The matrix assembly
  also performs any communication required so that the matrix can be
  used immediately after assembly.

  residual:  the residual of the governing equations
  A:         the Jacobian matrix
  alpha:     coefficient on the variables
  beta:      coefficient on the time-derivative terms
  gamma:     coefficient on the second time derivative term
  matOr:     the matrix orientation NORMAL or TRANSPOSE
*/
void TACSAssembler::assembleJacobian( TACSBVec *residual, 
				      TACSMat * A,
				      double alpha, double beta, double gamma,
				      MatrixOrientation matOr ){
  // Zero the residual and the matrix
  if (residual){ 
    residual->zeroEntries(); 
  }
  A->zeroEntries();

  // Sort the list of auxiliary elements - this call only performs the
  // sort if it is required (if new elements are added)
  if (auxElements){
    auxElements->sort();
  }

  // Run the p-threaded version of the assembly code
  if (thread_info->getNumThreads() > 1){
    // Set the number of completed elements to zero
    numCompletedElements = 0;
    tacsPInfo->tacs = this;
    tacsPInfo->res = residual;
    tacsPInfo->mat = A;
    tacsPInfo->alpha = alpha;
    tacsPInfo->beta = beta;
    tacsPInfo->gamma = gamma;
    tacsPInfo->matOr = matOr;

    // Create the joinable attribute
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    for ( int k = 0; k < thread_info->getNumThreads(); k++ ){
      pthread_create(&threads[k], &attr, 
		     TACSAssembler::assembleJacobian_thread,
                     (void*)tacsPInfo);
    }

    // Join all the threads
    for ( int k = 0; k < thread_info->getNumThreads(); k++ ){
      pthread_join(threads[k], NULL);
    }

    // Destroy the attribute
    pthread_attr_destroy(&attr);
  }
  else {
    // Retrieve pointers to temporary storage
    TacsScalar *vars, *dvars, *ddvars, *elemRes, *elemXpts;
    TacsScalar *elemWeights, *elemMat;
    getDataPointers(elementData, 
		    &vars, &dvars, &ddvars, &elemRes,
		    &elemXpts, NULL, &elemWeights, &elemMat);

    // Set the data for the auxiliary elements - if there are any
    int naux = 0, aux_count = 0;
    TACSAuxElem *aux = NULL;
    if (auxElements){
      naux = auxElements->getAuxElements(&aux);
    }

    for ( int i = 0; i < numElements; i++ ){
      int ptr = elementNodeIndex[i];
      int len = elementNodeIndex[i+1] - ptr;
      const int *nodes = &elementTacsNodes[ptr];
      xptVec->getValues(len, nodes, elemXpts);
      varsVec->getValues(len, nodes, vars);
      dvarsVec->getValues(len, nodes, dvars);
      ddvarsVec->getValues(len, nodes, ddvars);

      // Get the number of variables from the element
      int nvars = elements[i]->numVariables();

      // Compute and add the contributions to the residual
      if (residual){
        memset(elemRes, 0, nvars*sizeof(TacsScalar));
        elements[i]->addResidual(time, elemRes, elemXpts, 
                                 vars, dvars, ddvars);
      }

      // Compute and add the contributions to the Jacobian
      memset(elemMat, 0, nvars*nvars*sizeof(TacsScalar));
      elements[i]->addJacobian(time, elemMat, alpha, beta, gamma,
			       elemXpts, vars, dvars, ddvars);

      // Add the contribution to the residual and the Jacobian
      // from the auxiliary elements - if any
      while (aux_count < naux && aux[aux_count].num == i){
        if (residual){
          aux[aux_count].elem->addResidual(time, elemRes, elemXpts,
                                           vars, dvars, ddvars);
        }
        aux[aux_count].elem->addJacobian(time, elemMat, 
                                         alpha, beta, gamma,
                                         elemXpts, vars, dvars, ddvars);        
        aux_count++;
      }

      if (residual){
        residual->setValues(len, nodes, elemRes, ADD_VALUES);
      }
      addMatValues(A, i, elemMat, elementIData, elemWeights, matOr);
    }
  }

  // Do any matrix and residual assembly if required
  A->beginAssembly();
  if (residual){
    residual->beginSetValues(ADD_VALUES);
  }

  A->endAssembly();
  if (residual){
    residual->endSetValues(ADD_VALUES);
  }

  // Apply the boundary conditions
  if (residual){ 
    residual->applyBCs(varsVec); 
  }
  A->applyBCs();
}

/*!  
  Assemble a matrix of a specified type. Note that all matrices
  created from the TACSAssembler object have the same non-zero pattern
  and are interchangable.

  A:            the matrix to assemble (output)
  matType:      the matrix type defined in Element.h
  matOr:        the matrix orientation: NORMAL or TRANSPOSE
*/
void TACSAssembler::assembleMatType( ElementMatrixType matType,
                                     TACSMat *A, 
                                     MatrixOrientation matOr ){
  // Zero the matrix
  A->zeroEntries();

  if (thread_info->getNumThreads() > 1){
    // Set the number of completed elements to zero
    numCompletedElements = 0;    
    tacsPInfo->tacs = this;
    tacsPInfo->mat = A;
    tacsPInfo->matType = matType;
    tacsPInfo->matOr = matOr;

    // Create the joinable attribute
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    for ( int k = 0; k < thread_info->getNumThreads(); k++ ){
      pthread_create(&threads[k], &attr, 
		     TACSAssembler::assembleMatType_thread,
                     (void*)tacsPInfo);
    }

    // Join all the threads
    for ( int k = 0; k < thread_info->getNumThreads(); k++ ){
      pthread_join(threads[k], NULL);
    }

    // Destroy the attribute
    pthread_attr_destroy(&attr);
  }
  else {
    // Retrieve pointers to temporary storage
    TacsScalar *vars, *elemXpts, *elemMat, *elemWeights;
    getDataPointers(elementData, &vars, NULL, NULL, NULL,
		    &elemXpts, NULL, &elemWeights, &elemMat);

    for ( int i = 0; i < numElements; i++ ){
      // Retrieve the element variables and node locations
      int ptr = elementNodeIndex[i];
      int len = elementNodeIndex[i+1] - ptr;
      const int *nodes = &elementTacsNodes[ptr];
      xptVec->getValues(len, nodes, elemXpts);
      varsVec->getValues(len, nodes, vars);

      // Get the element matrix
      elements[i]->getMatType(matType, elemMat, elemXpts, vars);

      // Add the values into the element
      addMatValues(A, i, elemMat, elementIData, elemWeights, matOr);
    }
  }

  A->beginAssembly();
  A->endAssembly();
  A->applyBCs();
}

/*
  Initialize a list of functions
  
  Every function must be initialized - usually just once - before it
  can be evaluated. This is handled automatically within
  TACSAssembler.

  Check whether the functions are associated with this TACSAssembler
  object.  Next, call preInitalize() for each function in the list.
  Go through the function domain and call initialize for each element
  in the domain. Finally, call post initialize for each function in
  the list.

  functions:  an array of function values
  numFuncs:   the number of functions
*/
void TACSAssembler::initializeFunctions( TACSFunction **functions, 
					 int numFuncs ){
  // First check if this is the right assembly object
  for ( int k = 0; k < numFuncs; k++ ){
    if (this != functions[k]->getTACS()){
      fprintf(stderr, "[%d] Cannot evaluate function %s, wrong \
TACSAssembler object\n", mpiRank, functions[k]->functionName());
      return;
    }
  }

  // Test which functions have been initialized
  int count = 0;
  for ( int k = 0; k < numFuncs; k++ ){
    if (!functions[k]->isInitialized()){
      count++;
    }
  }

  if (count == 0){
    return;
  }

  // Create an index-list of the functions that haven't been
  // initialized yet
  int * list = new int[ count ];
  int j = 0;
  for ( int k = 0; k < numFuncs; k++ ){
    if (!functions[k]->isInitialized()){
      list[j] = k;
      j++;
    }
  }

  // Now initialize the functions
  for ( int k = 0; k < count; k++ ){
    functions[list[k]]->preInitialize();
  }
    
  for ( int k = 0; k < count; k++ ){
    if (functions[list[k]]->getDomain() == TACSFunction::ENTIRE_DOMAIN){
      for ( int i = 0; i < numElements; i++ ){
	functions[list[k]]->elementWiseInitialize(elements[i], i);
      }
    }
    else if (functions[list[k]]->getDomain() == TACSFunction::SUB_DOMAIN){
      const int * elementNums;
      int subDomainSize = functions[list[k]]->getElements(&elementNums);
    
      for ( int i = 0; i < subDomainSize; i++ ){
	int elemNum = elementNums[i];
        if (elemNum >= 0 && elemNum < numElements){
	  functions[list[k]]->elementWiseInitialize(elements[elemNum], elemNum);
	}
      }      
    }
  }
  
  for ( int k = 0; k < count; k++ ){
    functions[list[k]]->postInitialize();
  }

  delete [] list;
}

/*
  Evaluate a list of TACS functions

  First, check if the functions are initialized. Obtain the number of
  iterations over the function domain required to evaluate the
  functions.

  This function will print an error and return 0 if the underlying
  TACSAssembler object does not correspond to the TACSAssembler object.

  input:
  functions:  array of functions to evaluate
  numFuncs:   the number of functions to evaluate

  output:
  funcVals: the values of the functions 
*/
void TACSAssembler::evalFunctions( TACSFunction **functions, 
                                   int numFuncs, 
				   TacsScalar *funcVals ){
  // Find the max. number of iterations required
  int num_iters = 0;
  for ( int k = 0; k < numFuncs; k++ ){
    int iters = functions[k]->getNumIterations();
    if (iters > num_iters){
      num_iters = iters;
    }
  }

  // Retrieve pointers to temporary storage
  TacsScalar *vars, *elemXpts;
  getDataPointers(elementData, &vars, NULL, NULL, NULL,
		  &elemXpts, NULL, NULL, NULL);

  // check if initialization is neccessary
  initializeFunctions(functions, numFuncs);
     
  if (thread_info->getNumThreads() > 1){
    /*
    // Initialize the threads
    tacsPInfo->tacs = this;
    tacsPInfo->functions = functions;
    tacsPInfo->numFuncs = numFuncs;

    // Create the joinable attribute
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    for ( int iter = 0; iter < num_iters; iter++ ){
      // Initialize the pre-evaluation iterations
      for ( int k = 0; k < numFuncs; k++ ){
        functions[k]->preEval(iter);
      }

      tacsPInfo->funcIteration = iter;
      numCompletedElements = 0;

      for ( int k = 0; k < thread_info->getNumThreads(); k++ ){
        pthread_create(&threads[k], &attr, 
                       TACSAssembler::evalFunctions_thread,
                       (void*)tacsPInfo);
      }
      
      // Join all the threads
      for ( int k = 0; k < thread_info->getNumThreads(); k++ ){
        pthread_join(threads[k], NULL);
      }
    
      // Initialize the pre-evaluation iterations
      for ( int k = 0; k < numFuncs; k++ ){
        functions[k]->postEval(iter);
      }
    }
    */
  }
  else {
    for ( int iter = 0; iter < num_iters; iter++ ){
      // Initialize the pre-evaluation iterations
      for ( int k = 0; k < numFuncs; k++ ){
        functions[k]->preEval(iter);
      }
      
      // Default work arrays
      TacsScalar work_default[128];
      int iwork_default[128];

      for ( int k = 0; k < numFuncs; k++ ){
        // The work array pointers
        TacsScalar * work = NULL;
        int * iwork = NULL;
        
        // Get the size of the arrays
        int iwork_size = 0, work_size = 0;
        functions[k]->getEvalWorkSizes(&iwork_size, &work_size);
        
        // Check that the work arrays are sufficiently large
        if (work_size > 128){ work = new TacsScalar[work_size]; }
        else { work = work_default; }
        
        if (iwork_size > 128){ iwork = new int[iwork_size]; }
        else { iwork = iwork_default; }
      
        // Initialize the work arrays
        functions[k]->preEvalThread(iter, iwork, work);

        if (functions[k]->getDomain() == TACSFunction::ENTIRE_DOMAIN){
          for ( int i = 0; i < numElements; i++ ){
            // Determine the values of the state variables for the
            // current element
            int ptr = elementNodeIndex[i];
            int len = elementNodeIndex[i+1] - ptr;
            const int *nodes = &elementTacsNodes[ptr];
            xptVec->getValues(len, nodes, elemXpts);
            varsVec->getValues(len, nodes, vars);
            
            // Evaluate the element-wise component of the function
            functions[k]->elementWiseEval(iter, elements[i], i, 
					  elemXpts, vars, iwork, work);
          }
        }
        else if (functions[k]->getDomain() == TACSFunction::SUB_DOMAIN){
          const int * elementNums;
          int subDomainSize = functions[k]->getElements(&elementNums);
          
          for ( int i = 0; i < subDomainSize; i++ ){
            int elemNum = elementNums[i];
            
            if (elemNum >= 0 && elemNum < numElements){
              // Determine the values of the state variables 
              // for the current element
              int ptr = elementNodeIndex[elemNum];
              int len = elementNodeIndex[elemNum+1] - ptr;
              const int *nodes = &elementTacsNodes[ptr];
              xptVec->getValues(len, nodes, elemXpts);
              varsVec->getValues(len, nodes, vars);

              // Evaluate the element-wise component of the function
              functions[k]->elementWiseEval(iter, elements[elemNum], elemNum,
					    elemXpts, vars, iwork, work);
            }
          }
        }
      
        functions[k]->postEvalThread(iter, iwork, work);

        // Check that the work arrays are sufficiently large
        if (work_size > 128){ delete [] work; }
        if (iwork_size > 128){ delete [] iwork; }
      }
        
      // Initialize the pre-evaluation iterations
      for ( int k = 0; k < numFuncs; k++ ){
        functions[k]->postEval(iter);
      }
    }
  }

  for ( int k = 0; k < numFuncs; k++ ){
    funcVals[k] = functions[k]->getValue();
  }

  return;
}

/*
  Evaluate the derivative of a list of functions w.r.t. the design
  variables.

  Note that a function should be evaluated - using evalFunction - before
  its derivatives can be evaluated.

  The design variable sensitivities are divided into two distinct
  sets: material-dependent design variables and shape design
  variables. The shape design variables are handled through the
  TACSNodeMap class. The material-dependent design variables are
  handled through the element classes themselves.

  In this code, the derivative of the function w.r.t. the
  shape-dependent design variables is handled first. The derivative of
  the function w.r.t each nodal location is determined. The
  TACSNodeMap object (if not NULL) is then used to determine the
  derivative of the nodal locations w.r.t. the design variables
  themselves.
  
  The material-dependent design variables are handled on an
  element-by-element and traction-by-traction dependent basis.

  Note that this function distributes the result to the processors
  through a collective communication call. No further parallel
  communication is required.

  input:
  funcs:     the TACSFunction function objects
  numFuncs:  the number of functions - size of funcs array
  fdvSens:   the sensitivity - size numFuncs*numDVs
  numDVs:    the number of design variables
*/
void TACSAssembler::addDVSens( TACSFunction **funcs, int numFuncs, 
                               TacsScalar *fdvSens, int numDVs ){
  // Retrieve pointers to temporary storage
  TacsScalar *vars, *elemXpts, *elemXptSens;
  getDataPointers(elementData, &vars, NULL, NULL, NULL,
		  &elemXpts, NULL, NULL, &elemXptSens);

  // check if initialization is neccessary
  initializeFunctions(funcs, numFuncs);

  if (thread_info->getNumThreads() > 1){
    /*
    tacsPInfo->tacs = this;
    tacsPInfo->functions = funcs;
    tacsPInfo->numFuncs = numFuncs;
    tacsPInfo->numDesignVars = numDVs;
    tacsPInfo->fdvSens = fdvSens;

    // Create the joinable attribute
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    // Now compute df/dx
    numCompletedElements = 0;
    
    for ( int k = 0; k < thread_info->getNumThreads(); k++ ){
      pthread_create(&threads[k], &attr, 
		     TACSAssembler::evalDVSens_thread,
                     (void*)tacsPInfo);
    }
    
    // Join all the threads
    for ( int k = 0; k < thread_info->getNumThreads(); k++ ){
      pthread_join(threads[k], NULL);
    }

    // Destroy the attribute
    pthread_attr_destroy(&attr);
    */
  }
  else {
    // For each function, evaluate the derivative w.r.t. the 
    // design variables for each element
    for ( int k = 0; k < numFuncs; k++ ){
      TACSFunction * function = funcs[k];

      // Default work arrays
      TacsScalar * work = NULL;
      TacsScalar work_default[128];
      int work_size = function->getDVSensWorkSize();
      if (work_size > 128){ work = new TacsScalar[ work_size ]; }
      else { work = work_default; }

      if (function->getDomain() == TACSFunction::SUB_DOMAIN){
        // Get the function sub-domain
        const int * elemSubList;
        int numSubElems = function->getElements(&elemSubList);
      
        for ( int i = 0; i < numSubElems; i++ ){
          int elemNum = elemSubList[i];
          // Determine the values of the state variables for subElem
          int ptr = elementNodeIndex[elemNum];
          int len = elementNodeIndex[elemNum+1] - ptr;
          const int *nodes = &elementTacsNodes[ptr];
          xptVec->getValues(len, nodes, elemXpts);
          varsVec->getValues(len, nodes, vars);
          
          // Evaluate the element-wise sensitivity of the function
          function->elementWiseDVSens(&fdvSens[k*numDVs], numDVs,
                                      elements[elemNum], elemNum, 
                                      elemXpts, vars, work);	      
        }
      }
      else if (function->getDomain() == TACSFunction::ENTIRE_DOMAIN){
        for ( int elemNum = 0; elemNum < numElements; elemNum++ ){
          // Determine the values of the state variables for elemNum
          int ptr = elementNodeIndex[elemNum];
          int len = elementNodeIndex[elemNum+1] - ptr;
          const int *nodes = &elementTacsNodes[ptr];
          xptVec->getValues(len, nodes, elemXpts);
          varsVec->getValues(len, nodes, vars);
        
          // Evaluate the element-wise sensitivity of the function
          function->elementWiseDVSens(&fdvSens[k*numDVs], numDVs,
                                      elements[elemNum], elemNum, 
                                      elemXpts, vars, work);
        }
      }

      if (work_size > 128){ delete [] work; }
    }
  }
}

/*
  Evaluate the derivative of the function w.r.t. the owned nodes.

  This code evaluates the sensitivity of the function w.r.t. the 
  owned nodes for all elements in the function domain. 

  Note that a function should be evaluated - using evalFunction - before
  its derivatives can be evaluated.

  This function should be preferred to the use of evalDVSens without a 
  list of functions since it is more efficient!

  input:
  funcs:     the TACSFunction function objects
  numFuncs:  the number of functions - size of funcs array
  fXptSens:  the sensitivity
*/
void TACSAssembler::addXptSens( TACSFunction **funcs, int numFuncs, 
                                TACSBVec **fXptSens ){
  // First check if this is the right assembly object
  for ( int k = 0; k < numFuncs; k++ ){
    if (this != funcs[k]->getTACS()){
      fprintf(stderr, "[%d] Cannot evaluate function %s, wrong TACS object\n", 
              mpiRank, funcs[k]->functionName());
    }
  }

  // Retrieve pointers to temporary storage
  TacsScalar *vars, *elemXpts, *elemXptSens;
  getDataPointers(elementData, &vars, NULL, NULL, NULL,
		  &elemXpts, NULL, NULL, &elemXptSens);

  // check if initialization is neccessary
  initializeFunctions(funcs, numFuncs);
  if (thread_info->getNumThreads() > 1){
    /*
    tacsPInfo->tacs = this;
    tacsPInfo->functions = funcs;
    tacsPInfo->numFuncs = numFuncs;

    // Create the joinable attribute
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    // Compute the derivative of the functions w.r.t. the nodes
    numCompletedElements = 0;
    tacsPInfo->fXptSens = fXptSens;

    for ( int k = 0; k < thread_info->getNumThreads(); k++ ){
      pthread_create(&threads[k], &attr, 
		     TACSAssembler::evalXptSens_thread,
		     (void*)tacsPInfo);
    }

    // Join all the threads
    for ( int k = 0; k < thread_info->getNumThreads(); k++ ){
      pthread_join(threads[k], NULL);
    }
    */
  }
  else {
    // For each function, evaluate the derivative w.r.t. the 
    // nodal locations for all elements or part of the domain
    for ( int k = 0; k < numFuncs; k++ ){
      TACSFunction * function = funcs[k];
    
      // Default work arrays
      TacsScalar * work = NULL;
      TacsScalar work_default[128];
      int work_size = function->getXptSensWorkSize();
      if (work_size > 128){ work = new TacsScalar[ work_size ]; }
      else { work = work_default; }

      if (function->getDomain() == TACSFunction::SUB_DOMAIN){
	// Get the function sub-domain
	const int * elemSubList;
	int numSubElems = function->getElements(&elemSubList);
	for ( int i = 0; i < numSubElems; i++ ){
	  int elemNum = elemSubList[i];
	  // Determine the values of the state variables for subElem
          int ptr = elementNodeIndex[elemNum];
          int len = elementNodeIndex[elemNum+1] - ptr;
          const int *nodes = &elementTacsNodes[ptr];
          xptVec->getValues(len, nodes, elemXpts);
          varsVec->getValues(len, nodes, vars);
          
	  // Evaluate the element-wise sensitivity of the function
	  function->elementWiseXptSens(elemXptSens, 
				       elements[elemNum], elemNum, 
				       elemXpts, vars, work);
	  fXptSens[k]->setValues(len, nodes, elemXptSens, ADD_VALUES);
	}
      }
      else if (function->getDomain() == TACSFunction::ENTIRE_DOMAIN){
	for ( int elemNum = 0; elemNum < numElements; elemNum++ ){
	  // Determine the values of the state variables for elemNum
          int ptr = elementNodeIndex[elemNum];
          int len = elementNodeIndex[elemNum+1] - ptr;
          const int *nodes = &elementTacsNodes[ptr];
          xptVec->getValues(len, nodes, elemXpts);
          varsVec->getValues(len, nodes, vars);
      
	  // Evaluate the element-wise sensitivity of the function
	  function->elementWiseXptSens(elemXptSens, 
				       elements[elemNum], elemNum, 
				       elemXpts, vars, work);
	  fXptSens[k]->setValues(len, nodes, elemXptSens, ADD_VALUES);
	}
      }
  
      if (work_size > 128){ delete [] work; }
    }
  }
}

/*
  Evaluate the derivative of the function w.r.t. the state variables.

  This code evaluates the sensitivity of the function w.r.t. the 
  state variables for all elements in the function domain. This code
  is usually much faster than the code for computing the derivative of 
  the function w.r.t. the design variables. 

  Note that the sensitivity vector 'vec' is assembled, and appropriate
  boundary conditions are imposed before the function is returned.

  function: the function pointer
  vec:      the derivative of the function w.r.t. the state variables
*/
void TACSAssembler::addSVSens( TACSFunction **funcs, int numFuncs, 
                               TACSBVec **vec ){
  // First check if this is the right assembly object
  for ( int k = 0; k < numFuncs; k++ ){
    if (this != funcs[k]->getTACS()){
      fprintf(stderr, "[%d] Cannot evaluate function %s, wrong TACS object\n", 
              mpiRank, funcs[k]->functionName());
    }
  }

  // Retrieve pointers to temporary storage
  TacsScalar *vars, *elemRes, *elemXpts;
  getDataPointers(elementData, &vars, &elemRes, NULL, NULL,
		  &elemXpts, NULL, NULL, NULL);

  // Perform the initialization if neccessary
  initializeFunctions(funcs, numFuncs);
  
  for ( int k = 0; k < numFuncs; k++ ){
    TACSFunction * function = funcs[k];

    // Default work arrays
    TacsScalar * work = NULL;
    TacsScalar work_default[128];
    int work_size = function->getSVSensWorkSize();
    if (work_size > 128){ work = new TacsScalar[ work_size ]; }
    else { work = work_default; }
    
    if (function->getDomain() == TACSFunction::ENTIRE_DOMAIN){
      for ( int i = 0; i < numElements; i++ ){
        // Determine the values of the state variables for subElem
        int ptr = elementNodeIndex[i];
        int len = elementNodeIndex[i+1] - ptr;
        const int *nodes = &elementTacsNodes[ptr];
        xptVec->getValues(len, nodes, elemXpts);
        varsVec->getValues(len, nodes, vars);
        
        // Evaluate the element-wise sensitivity of the function
        function->elementWiseSVSens(elemRes, elements[i], i, 
                                    elemXpts, vars, work);
        vec[k]->setValues(len, nodes, elemRes, ADD_VALUES);
      }
    }
    else if (function->getDomain() == TACSFunction::SUB_DOMAIN){
      const int * elementNums;
      int subDomainSize = function->getElements(&elementNums);
      
      for ( int i = 0; i < subDomainSize; i++ ){
        int elemNum = elementNums[i];
        if (elemNum >= 0 && elemNum < numElements){	
          // Determine the values of the state variables for the current element
          int ptr = elementNodeIndex[elemNum];
          int len = elementNodeIndex[elemNum+1] - ptr;
          const int *nodes = &elementTacsNodes[ptr];
          xptVec->getValues(len, nodes, elemXpts);
          varsVec->getValues(len, nodes, vars);
          
          // Evaluate the sensitivity
          function->elementWiseSVSens(elemRes, elements[elemNum], elemNum, 
                                      elemXpts, vars, work);
          vec[k]->setValues(len, nodes, elemRes, ADD_VALUES);
        }
      }
    }

    if (work_size > 128){ delete [] work; }

    // Add the values into the array
    vec[k]->beginSetValues(ADD_VALUES);
  }

  // Finish adding the values
  for ( int k = 0; k < numFuncs; k++ ){
    vec[k]->endSetValues(ADD_VALUES);
    vec[k]->applyBCs();
  }
}

/*
  Evaluate the product of several ajdoint vectors with the derivative
  of the residual w.r.t. the design variables.

  This function is collective on all TACSAssembler processes. This
  computes the product of the derivative of the residual w.r.t. the
  design variables with several adjoint vectors simultaneously. This
  saves computational time as the derivative of the element residuals
  can be reused for each adjoint vector. This function performs the
  same task as evalAdjointResProduct, but uses more memory than
  calling it for each adjoint vector.

  adjoint:     the array of adjoint vectors
  numAdjoints: the number of adjoint vectors
  dvSens:      product of the derivative of the residuals and the adjoint
  numDVs:      the number of design variables
*/
void TACSAssembler::addAdjointResProducts( TACSBVec **adjoint, 
                                           int numAdjoints, 
                                           TacsScalar *fdvSens, 
                                           int numDVs ){
  for ( int k = 0; k < numAdjoints; k++ ){
    adjoint[k]->beginDistributeValues();
  }
  for ( int k = 0; k < numAdjoints; k++ ){
    adjoint[k]->endDistributeValues();
  }

  // Sort the list of auxiliary elements - this only performs the
  // sort if it is required (if new elements are added)
  if (auxElements){
    auxElements->sort();
  }

  if (thread_info->getNumThreads() > 1){
    /*
    tacsPInfo->tacs = this;
    tacsPInfo->numAdjoints = numAdjoints;
    tacsPInfo->adjointVars = localAdjoint;
    tacsPInfo->numDesignVars = numDVs;
    tacsPInfo->fdvSens = dvSensVals;

    // Create the joinable attribute
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    // Now compute Phi^{T} * dR/dx 
    numCompletedElements = 0;

    for ( int k = 0; k < thread_info->getNumThreads(); k++ ){
      pthread_create(&threads[k], &attr, 
                     TACSAssembler::adjointResProduct_thread,
                     (void*)tacsPInfo);
    }
    
    // Join all the threads
    for ( int k = 0; k < thread_info->getNumThreads(); k++ ){
      pthread_join(threads[k], NULL);
    }

    // Destroy the attribute
    pthread_attr_destroy(&attr);
    */
  }
  else {
    // Retrieve pointers to temporary storage
    TacsScalar *vars, *dvars, *ddvars;
    TacsScalar *elemXpts, *elemAdjoint;
    getDataPointers(elementData, &vars, &dvars, &ddvars, &elemAdjoint,
		    &elemXpts, NULL, NULL, NULL);

    // Set the data for the auxiliary elements - if there are any
    int naux = 0, aux_count = 0;
    TACSAuxElem *aux = NULL;
    if (auxElements){
      naux = auxElements->getAuxElements(&aux);
    }

    for ( int i = 0; i < numElements; i++ ){
      // Find the variables and nodes
      int ptr = elementNodeIndex[i];
      int len = elementNodeIndex[i+1] - ptr;
      const int *nodes = &elementTacsNodes[ptr];
      xptVec->getValues(len, nodes, elemXpts);
      varsVec->getValues(len, nodes, vars);
      dvarsVec->getValues(len, nodes, dvars);
      ddvarsVec->getValues(len, nodes, ddvars);
      
      // Get the adjoint variables
      for ( int k = 0; k < numAdjoints; k++ ){
	double scale = 1.0;
	adjoint[k]->getValues(len, nodes, elemAdjoint);
	elements[i]->addAdjResProduct(time, scale, 
                                      &fdvSens[k*numDVs], numDVs,
				      elemAdjoint, elemXpts,
				      vars, dvars, ddvars);

        // Add the contribution from the auxiliary elements
        while (aux_count < naux && aux[aux_count].num == i){
          aux[aux_count].elem->addAdjResProduct(time, scale,
                                                &fdvSens[k*numDVs], numDVs,
                                                elemAdjoint, elemXpts,
                                                vars, dvars, ddvars);
          aux_count++;
        }
      }
    }
  }
}

/*
  Evaluate the product of several ajdoint vectors with the derivative
  of the residual w.r.t. the nodal points.

  This function is collective on all TACSAssembler processes. This
  computes the product of the derivative of the residual w.r.t. the
  nodal points with several adjoint vectors simultaneously. This
  saves computational time as the derivative of the element residuals
  can be reused for each adjoint vector. 

  adjoint:     the array of adjoint vectors
  numAdjoints: the number of adjoint vectors
  dvSens:      the product of the derivative of the residuals and the adjoint
  numDVs:      the number of design variables
*/
void TACSAssembler::addAdjointResXptSensProducts( TACSBVec **adjoint, 
                                                  int numAdjoints,
                                                  TACSBVec **adjXptSens ){

  for ( int k = 0; k < numAdjoints; k++ ){
    adjoint[k]->beginDistributeValues();
  }
  for ( int k = 0; k < numAdjoints; k++ ){
    adjoint[k]->endDistributeValues();
  }

  // Sort the list of auxiliary elements - this only performs the
  // sort if it is required (if new elements are added)
  if (auxElements){
    auxElements->sort();
  }

  if (thread_info->getNumThreads() > 1){
    /*
    tacsPInfo->tacs = this;
    tacsPInfo->numAdjoints = numAdjoints;
    tacsPInfo->adjointVars = localAdjoint;

    // Create the joinable attribute
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    // Set the vector to store Phi^{T} dR/dXpts
    numCompletedElements = 0;
    tacsPInfo->adjXptSensProduct = adjXptSensProduct;

    for ( int k = 0; k < thread_info->getNumThreads(); k++ ){
      pthread_create(&threads[k], &attr, 
		     TACSAssembler::adjointResXptSensProduct_thread,
		     (void*)tacsPInfo);
    }
    
    // Join all the threads
    for ( int k = 0; k < thread_info->getNumThreads(); k++ ){
      pthread_join(threads[k], NULL);
    }
    */
  }
  else {
    // Retrieve pointers to temporary storage
    TacsScalar *vars, *dvars, *ddvars;
    TacsScalar *elemXpts, *elemAdjoint, *xptSens;
    getDataPointers(elementData, &vars, &dvars, &ddvars, &elemAdjoint,
		    &elemXpts, &xptSens, NULL, NULL);

    // Set the data for the auxiliary elements - if there are any
    int naux = 0, aux_count = 0;
    TACSAuxElem *aux = NULL;
    if (auxElements){
      naux = auxElements->getAuxElements(&aux);
    }

    for ( int i = 0; i < numElements; i++ ){
      // Find the variables and nodes
      int ptr = elementNodeIndex[i];
      int len = elementNodeIndex[i+1] - ptr;
      const int *nodes = &elementTacsNodes[ptr];
      xptVec->getValues(len, nodes, elemXpts);
      varsVec->getValues(len, nodes, vars);
      dvarsVec->getValues(len, nodes, dvars);
      ddvarsVec->getValues(len, nodes, ddvars);
      
      // Get the adjoint variables
      for ( int k = 0; k < numAdjoints; k++ ){
        memset(xptSens, 0, TACS_SPATIAL_DIM*len*sizeof(TacsScalar));
	adjoint[k]->getValues(len, nodes, elemAdjoint);
	elements[i]->addAdjResXptProduct(time, xptSens,
                                         elemAdjoint, elemXpts,
                                         vars, dvars, ddvars);

        // Add the contribution from the auxiliary elements
        while (aux_count < naux && aux[aux_count].num == i){
          aux[aux_count].elem->addAdjResXptProduct(time, xptSens,
                                                   elemAdjoint, elemXpts,
                                                   vars, dvars, ddvars);
          aux_count++;
        }

        adjXptSens[k]->setValues(len, nodes, xptSens, ADD_VALUES);
      }
    }   
  }
}

/*
  Evaluate the derivative of an inner product of two vectors with a
  matrix of a given type. This code does not explicitly evaluate the
  element matrices.  Instead, the inner product contribution from each
  element matrix is added to the final result. This implementation
  saves considerable computational time and memory.

  input:
  matType:   the matrix type
  psi:       the left-multiplying vector
  phi:       the right-multiplying vector
  numDVs:    the length of the design variable array

  output:
  dvSens:    the derivative of the inner product 
*/
void TACSAssembler::addMatDVSensInnerProduct( TacsScalar scale, 
                                              ElementMatrixType matType, 
                                              TACSBVec *psi, TACSBVec *phi,
                                              TacsScalar *fdvSens, int numDVs ){
  psi->beginDistributeValues();
  phi->beginDistributeValues();
  psi->endDistributeValues();
  phi->endDistributeValues();

  // Retrieve pointers to temporary storage
  TacsScalar *elemVars, *elemPsi, *elemPhi, *elemXpts;
  getDataPointers(elementData, &elemVars, &elemPsi, &elemPhi, NULL,
		  &elemXpts, NULL, NULL, NULL);

  // Go through each element in the domain and compute the derivative
  // of the residuals with respect to each design variable and multiply by
  // the adjoint variables
  for ( int i = 0; i < numElements; i++ ){
    // Find the variables and nodes
    int ptr = elementNodeIndex[i];
    int len = elementNodeIndex[i+1] - ptr;
    const int *nodes = &elementTacsNodes[ptr];
    xptVec->getValues(len, nodes, elemXpts);
    varsVec->getValues(len, nodes, elemVars);
    psi->getValues(len, nodes, elemPsi);
    phi->getValues(len, nodes, elemPhi);
    
    // Add the contribution to the design variable vector
    elements[i]->addMatDVSensInnerProduct(matType, scale, fdvSens, numDVs,
					  elemPsi, elemPhi, elemXpts,
					  elemVars);
  }
}

/*
  Evaluate the derivative of the inner product of two vectors with a
  matrix with respect to the state variables. This is only defined for
  nonlinear matrices, like the geometric stiffness matrix.  Instead of
  computing the derivative of the matrix for each vector component and
  then computing the inner product, this code computes the derivative
  of the inner product directly, saving computational time and memory.

  input:
  scale:     the scaling parameter applied to the derivative
  matType:   the matrix type
  psi:       the left-multiplying vector
  phi:       the right-multiplying vector
  numDVs:    the length of the design variable array

  output:
  res:       the derivative of the inner product w.r.t. the state vars
*/
void TACSAssembler::evalMatSVSensInnerProduct( TacsScalar scale, 
					       ElementMatrixType matType, 
					       TACSBVec *psi, TACSBVec *phi, 
                                               TACSBVec *res ){
  // Zero the entries in the residual vector
  res->zeroEntries();

  // Distribute the variable values
  psi->beginDistributeValues();
  phi->beginDistributeValues();
  psi->endDistributeValues();
  phi->endDistributeValues();

  // Retrieve pointers to temporary storage
  TacsScalar *elemVars, *elemPsi, *elemPhi, *elemRes, *elemXpts;
  getDataPointers(elementData, &elemVars, &elemPsi, &elemPhi, &elemRes,
		  &elemXpts, NULL, NULL, NULL);

  // Go through each element in the domain and compute the derivative
  // of the residuals with respect to each design variable and multiply by
  // the adjoint variables
  for ( int i = 0; i < numElements; i++ ){
    // Find the variables and nodes
    int ptr = elementNodeIndex[i];
    int len = elementNodeIndex[i+1] - ptr;
    const int *nodes = &elementTacsNodes[ptr];
    xptVec->getValues(len, nodes, elemXpts);
    varsVec->getValues(len, nodes, elemVars);
    psi->getValues(len, nodes, elemPsi);
    phi->getValues(len, nodes, elemPhi);

    // Zero the residual
    int nvars = varsPerNode*len;
    memset(elemRes, 0, nvars*sizeof(TacsScalar));

    // Add the contribution to the design variable vector
    elements[i]->getMatSVSensInnerProduct(matType, elemRes,
					  elemPsi, elemPhi, elemXpts,
					  elemVars);

    // Add the residual values to the local residual array
    res->setValues(len, nodes, elemRes, ADD_VALUES);
  }

  res->beginSetValues(ADD_VALUES);
  res->endSetValues(ADD_VALUES);

  // Apply the boundary conditions to the fully assembled vector
  res->applyBCs();
}

/*
  Evaluate the matrix-free Jacobian-vector product of the input vector
  x and store the result in the output vector y.

  This code does not assemble a matrix, but does compute the
  element-wise matricies. This code is not a finite-difference
  matrix-vector product implementation.

  Since the element Jacobian matrices are computed exactly, we can
  evaluate either a regular matrix-product or the transpose matrix
  product.

  input:
  scale:     the scalar coefficient
  alpha:     coefficient on the variables
  beta:      coefficient on the time-derivative terms
  gamma:     coefficient on the second time derivative term
  x:         the input vector
  matOr:     the matrix orientation
  
  output:
  y:         the output vector y <- y + scale*J^{Op}*x
*/
void TACSAssembler::addJacobianVecProduct( TacsScalar scale, 
                                           double alpha, double beta, double gamma,
                                           TACSBVec *x, TACSBVec *y,
                                           MatrixOrientation matOr ){
  x->beginDistributeValues();
  x->endDistributeValues();

  // Retrieve pointers to temporary storage
  TacsScalar *vars, *dvars, *ddvars, *yvars, *elemXpts;
  TacsScalar *elemWeights, *elemMat;
  getDataPointers(elementData, 
                  &vars, &dvars, &ddvars, &yvars,
                  &elemXpts, NULL, &elemWeights, &elemMat);
  
  // Set the data for the auxiliary elements - if there are any
  int naux = 0, aux_count = 0;
  TACSAuxElem *aux = NULL;
  if (auxElements){
    naux = auxElements->getAuxElements(&aux);
  }

  // Loop over all the elements in the model
  for ( int i = 0; i < numElements; i++ ){
    int ptr = elementNodeIndex[i];
    int len = elementNodeIndex[i+1] - ptr;
    const int *nodes = &elementTacsNodes[ptr];
    xptVec->getValues(len, nodes, elemXpts);
    varsVec->getValues(len, nodes, vars);
    dvarsVec->getValues(len, nodes, dvars);
    ddvarsVec->getValues(len, nodes, ddvars);

    // Get the number of variables from the element
    int nvars = elements[i]->numVariables();
    
    // Compute and add the contributions to the Jacobian
    memset(elemMat, 0, nvars*nvars*sizeof(TacsScalar));
    elements[i]->addJacobian(time, elemMat, alpha, beta, gamma,
                             elemXpts, vars, dvars, ddvars);

    // Add the contribution to the residual and the Jacobian
    // from the auxiliary elements - if any
    while (aux_count < naux && aux[aux_count].num == i){
      aux[aux_count].elem->addJacobian(time, elemMat, 
                                       alpha, beta, gamma,
                                       elemXpts, vars, dvars, ddvars);      
      aux_count++;
    }

    // Temporarily set the variable array as the element input array
    // and get the local variable input values from the local array.
    TacsScalar *xvars = vars;
    x->getValues(len, nodes, xvars);
   
    // Take the matrix vector product. Note the matrix is stored in
    // row-major order and BLAS assumes column-major order. As a
    // result, the transpose arguments are reversed.
    TacsScalar zero = 0.0;
    int incx = 1;
    if (matOr == NORMAL){
      BLASgemv("T", &nvars, &nvars, &scale, elemMat, &nvars, 
               xvars, &incx, &zero, yvars, &incx);
    }
    else {
      BLASgemv("N", &nvars, &nvars, &scale, elemMat, &nvars, 
               xvars, &incx, &zero, yvars, &incx);
    }

    // Add the residual values
    y->setValues(len, nodes, yvars, ADD_VALUES);
  }

  // Add the dependent-variable residual from the dependent nodes
  y->beginSetValues(ADD_VALUES);
  y->endSetValues(ADD_VALUES);

  // Set the boundary conditions
  y->applyBCs();
}

/*
  Test the implementation of the given element number.

  This tests the stiffness matrix and various parts of the
  design-sensitivities: the derivative of the determinant of the
  Jacobian, the derivative of the strain w.r.t. the nodal coordinates,
  and the state variables and the derivative of the residual w.r.t.
  the design variables and nodal coordiantes.

  elemNum:     the element number to test
  print_level: the print level to use   
*/
void TACSAssembler::testElement( int elemNum, int print_level ){
  if (!meshInitializedFlag){
    fprintf(stderr, "[%d] Cannot call testElement() before initialize()\n", 
	    mpiRank);
    return;
  }
  else if (elemNum < 0 || elemNum >= numElements){
    fprintf(stderr, "[%d] Element number %d out of range [0,%d)\n",
	    mpiRank, elemNum, numElements);
    return;
  }

  // Retrieve pointers to temporary storage
  TacsScalar *elemXpts;
  getDataPointers(elementData, NULL, NULL, NULL, NULL,
		  &elemXpts, NULL, NULL, NULL);

  int ptr = elementNodeIndex[elemNum];
  int len = elementNodeIndex[elemNum+1] - ptr;
  const int *nodes = &elementTacsNodes[ptr];
  xptVec->getValues(len, nodes, elemXpts);

  // Create the element test function
  double pt[] = {0.0, 0.0, 0.0};
  TestElement * test = new TestElement(elements[elemNum],
				       elemXpts);
  test->incref();
  test->setPrintLevel(print_level);
  
  printf("Testing element %s\n", elements[elemNum]->elementName());
  if (test->testJacobian()){ printf("Stiffness matrix failed\n"); }
  else { printf("Stiffness matrix passed\n"); }
  if (test->testJacobianXptSens(pt)){ printf("Jacobian XptSens failed\n"); }
  else { printf("Jacobian XptSens passed\n"); }
  if (test->testStrainSVSens(pt)){ printf("Strain SVSens failed\n"); }
  else { printf("Strain SVSens passed\n"); }
  /*if
 (test->testResXptSens()){ printf("Res XptSens failed\n"); }
  else { printf("Res XptSens passed\n"); }
  if (test->testStrainXptSens(pt)){ printf("Strain XptSens failed\n"); }
  else { printf("Strain XptSens passed\n"); }
  if (test->testResDVSens()){ printf("Res DVSens failed\n"); }
  else { printf("Res DVSens passed\n"); }
  if (test->testMatDVSens(STIFFNESS_MATRIX)){
    printf("Stiffness matrix DVSens failed\n"); }
  else { printf("Stiffness Matrix DVSens passed\n"); }
  if (test->testMatDVSens(MASS_MATRIX)){
    printf("Mass matrix DVSens failed\n"); }
  else { printf("Mass Matrix DVSens passed\n"); }
  if (test->testMatDVSens(GEOMETRIC_STIFFNESS_MATRIX)){ 
    printf("Geometric stiffness matrix DVSens failed\n"); }
  else { printf("Geometric stiffness Matrix DVSens passed\n"); }
  */
  printf("\n");
  
  test->decref();
}

/*
  Test the implementation of the given element's constitutive class.
  
  This function tests the failure computation and the mass
  sensitivities for the given element.

  elemNum:     the element to retrieve the constitutive object from
  print_level: the print level to use for the test
*/
void TACSAssembler::testConstitutive( int elemNum, int print_level ){
  if (!meshInitializedFlag){
    fprintf(stderr, "[%d] Cannot call testConstitutive() before initialize()\n", 
	    mpiRank);
    return;
  }
  else if (elemNum < 0 || elemNum >= numElements){
    fprintf(stderr, "[%d] Element number %d out of range [0,%d)\n",
	    mpiRank, elemNum, numElements);
    return;
  }

  TACSConstitutive * stiffness = elements[elemNum]->getConstitutive();
  
  if (stiffness){
    double pt[] = {0.0, 0.0, 0.0};
    TestConstitutive * test = new TestConstitutive(stiffness);
    test->incref();
    test->setPrintLevel(print_level);
    
    printf("Testing constitutive class %s\n", stiffness->constitutiveName());
    if (test->testFailStrainSens(pt)){ printf("Fail StrainSens failed\n"); }
    else { printf("Fail StrainSens passed\n"); }
    if (test->testBucklingStrainSens()){ printf("Buckling StrainSens failed\n"); }
    else { printf("Buckling StrainSens passed\n"); }
    /*
    if (test->testMassDVSens(pt)){ printf("Mass DVSens failed\n"); }
    else { printf("Mass DVSens passed\n"); }
    if (test->testFailDVSens(pt)){ printf("Fail DVSens failed\n"); }
    else { printf("Fail DVSens passed\n"); }
    if (test->testBucklingDVSens()){ printf("Buckling DVSens failed\n"); }
    else { printf("Buckling DVSens passed\n"); }
    */
    printf("\n");
 
    test->decref();
  }
}

/*
  Test the implementation of the function. 

  This tests the state variable sensitivities and the design variable
  sensitivities of the function of interest. These sensitivities are
  computed based on a random perturbation of the input values.  Note
  that a system of equations should be solved - or the variables
  should be set randomly before calling this function, otherwise this
  function may produce unrealistic function values.
  
  Note that this function uses a central difference if the real code
  is compiled, and a complex step approximation if the complex version
  of the code is used.

  func:            the function to test
  num_design_vars: the number of design variables to use
  dh:              the step size to use
*/
void TACSAssembler::testFunction( TACSFunction *func, 
				  int num_design_vars,
				  double dh ){
  if (!meshInitializedFlag){
    fprintf(stderr, 
            "[%d] Cannot call testFunction() before initialize()\n", 
	    mpiRank);
    return;
  }

  // First, test the design variable values
  TacsScalar * x = new TacsScalar[ num_design_vars ];
  TacsScalar * xpert = new TacsScalar[ num_design_vars ];
  TacsScalar * xtemp = new TacsScalar[ num_design_vars ];

  for ( int k = 0; k < num_design_vars; k++ ){
    xpert[k] = (1.0*rand())/RAND_MAX;
  }

  MPI_Bcast(xpert, num_design_vars, TACS_MPI_TYPE, 0, tacs_comm);

  getDesignVars(x, num_design_vars);
  setDesignVars(x, num_design_vars);

  TacsScalar fd = 0.0;
#ifdef TACS_USE_COMPLEX
  // Compute the function at the point x + dh*xpert
  for ( int k = 0; k < num_design_vars; k++ ){
    xtemp[k] = x[k] + TacsScalar(0.0, dh)*xpert[k];
  }
  setDesignVars(xtemp, num_design_vars);
  evalFunctions(&func, 1, &fd);
  fd = ImagPart(fd)/dh;
#else
  // Compute the function at the point x + dh*xpert
  for ( int k = 0; k < num_design_vars; k++ ){
    xtemp[k] = x[k] + dh*xpert[k];
  }
  setDesignVars(xtemp, num_design_vars);
  TacsScalar fval0;
  evalFunctions(&func, 1, &fval0);

  // Compute the function at the point x - dh*xpert
  for ( int k = 0; k < num_design_vars; k++ ){
    xtemp[k] = x[k] - dh*xpert[k];
  }
  setDesignVars(xtemp, num_design_vars);
  TacsScalar fval1;
  evalFunctions(&func, 1, &fval1);
  fd = 0.5*(fval0 - fval1)/dh;
#endif // TACS_USE_COMPLEX

  // Compute df/dx
  TacsScalar ftmp;
  setDesignVars(x, num_design_vars);
  evalFunctions(&func, 1, &ftmp);
  addDVSens(&func, 1, xtemp, num_design_vars);
  
  // Compute df/dx^{T} * xpert
  TacsScalar pdf = 0.0;
  for ( int k = 0; k < num_design_vars; k++ ){
    pdf += xtemp[k]*xpert[k];
  }

  if (mpiRank == 0){
    fprintf(stderr, "Testing function %s\n", func->functionName());
    const char * descript = "df/dx^{T}p";
    fprintf(stderr, "%*s[   ] %15s %15s %15s\n",
	    (int)strlen(descript), "Val", "Analytic", 
            "Approximate", "Rel. Error");
    if (pdf != 0.0){
      fprintf(stderr, "%s[%3d] %15.6e %15.6e %15.4e\n", 
              descript, 0, RealPart(pdf), RealPart(fd), 
              fabs(RealPart((pdf - fd)/pdf)));
    }
    else {
      fprintf(stderr, "%s[%3d] %15.6e %15.6e\n", 
              descript, 0, RealPart(pdf), RealPart(fd));
    }
  }

  delete [] x;
  delete [] xtemp;
  delete [] xpert;
  
  TACSBVec *temp = createVec();
  TACSBVec *pert = createVec();
  TACSBVec *vars = createVec();
  temp->incref();
  pert->incref();
  vars->incref();

  getVariables(vars, NULL, NULL);

  // Set up a random perturbation 
  pert->setRand(-1.0, 1.0);
  pert->applyBCs();
  pert->scale(vars->norm()/pert->norm());

#ifdef TACS_USE_COMPLEX
  // Evaluate the function at vars + dh*pert
  temp->copyValues(vars);
  temp->axpy(TacsScalar(0.0, dh), pert);
  setVariables(temp);

  evalFunctions(&func, 1, &fd);
  fd = ImagPart(fd)/dh;
#else
  // Evaluate the function at vars + dh*pert
  temp->copyValues(vars);
  temp->axpy(dh, pert);
  setVariables(temp);
  evalFunctions(&func, 1, &fval0);

  // Evaluate the function at vars - dh*pert
  temp->copyValues(vars);
  temp->axpy(-dh, pert);
  setVariables(temp);
  evalFunctions(&func, 1, &fval1);
  
  fd = 0.5*(fval0 - fval1)/dh;
#endif // TACS_USE_COMPLEX

  // Reset the variable values
  setVariables(vars);

  // Evaluate the state variable sensitivity
  evalFunctions(&func, 1, &ftmp);
  addSVSens(&func, 1, &temp);
  pdf = temp->dot(pert);

  if (mpiRank == 0){
    const char * descript = "df/du^{T}p";
    fprintf(stderr, "%*s[   ] %15s %15s %15s\n",
	    (int)strlen(descript), "Val", "Analytic", 
            "Approximate", "Rel. Error");
    if (pdf != 0.0){
      fprintf(stderr, "%s[%3d] %15.6e %15.6e %15.4e\n", 
              descript, 0, RealPart(pdf), RealPart(fd), 
              fabs(RealPart((pdf - fd)/pdf)));
    }
    else {
      fprintf(stderr, "%s[%3d] %15.6e %15.6e\n", 
              descript, 0, RealPart(pdf), RealPart(fd));
    }
  }

  temp->decref();
  pert->decref();
  vars->decref();  
}

/*!  
  Determine the number of components defined by elements in the
  TACSAssembler object.

  This call is collective - the number of components is obtained
  by a global reduction.
*/
int TACSAssembler::getNumComponents(){
  // Find the maximum component number on this processor
  int max_comp_num = 0;
  for ( int i = 0; i < numElements; i++ ){
    if (elements[i]->getComponentNum() >= max_comp_num){
      max_comp_num = elements[i]->getComponentNum();
    }
  }
  max_comp_num++;

  int num_components = 1;
  MPI_Allreduce(&max_comp_num, &num_components, 1, MPI_INT, 
                MPI_MAX, tacs_comm);
  return num_components;
}

/*
  Return the output nodal ranges. These may be used to determine what
  range of node numbers need to be determined by this process.  
*/
void TACSAssembler::getOutputNodeRange( enum ElementType elem_type,
					int ** _node_range ){
  int nelems = 0, nodes = 0, ncsr = 0;
  for ( int i = 0; i < numElements; i++ ){
    if (elements[i]->getElementType() == elem_type){
      elements[i]->addOutputCount(&nelems, &nodes, &ncsr);
    }
  }

  int * node_range = new int[ mpiSize+1 ];
  node_range[0] = 0;
  MPI_Allgather(&nodes, 1, MPI_INT, &node_range[1], 1, MPI_INT, tacs_comm);

  for ( int i = 0; i < mpiSize; i++ ){
    node_range[i+1] += node_range[i];
  }
  *_node_range = node_range;
}

/*
  Given the element type, determine the connectivity of the global
  data structure. Record the component number for each element within
  the data structure

  input:
  elem_type: the type of element eg SHELL, EULER_BEAM, SOLID etc.
  (see all the element types in Element.h)

  output:
  component_nums: an array of size nelems of the component numbers
  csr:            the csr element->nodes information for this class
  csr_range:      the range of csr data on this processor
  node_range:     the range of nodal values on this processor
*/
void TACSAssembler::getOutputConnectivity( enum ElementType elem_type,
                                           int ** component_nums,
                                           int ** _csr, int ** _csr_range, 
					   int ** _node_range ){
  // First go through and count up the number of elements and the 
  // size of the connectivity array required
  int nelems = 0, nodes = 0, ncsr = 0;
  for ( int i = 0; i < numElements; i++ ){
    if (elements[i]->getElementType() == elem_type){
      elements[i]->addOutputCount(&nelems, &nodes, &ncsr);
    }
  }

  int *node_range = new int[ mpiSize+1 ];
  int *csr_range = new int[ mpiSize+1 ];
  node_range[0] = 0;
  MPI_Allgather(&nodes, 1, MPI_INT, &node_range[1], 1, MPI_INT, tacs_comm);
  csr_range[0] = 0;
  MPI_Allgather(&ncsr, 1, MPI_INT, &csr_range[1], 1, MPI_INT, tacs_comm);

  for ( int i = 0; i < mpiSize; i++ ){
    node_range[i+1] += node_range[i];
    csr_range[i+1]  += csr_range[i];
  }

  int *csr = new int[ ncsr ];
  int *comp_nums = new int[ nelems ];
  ncsr = 0;
  nelems = 0;
  nodes = node_range[mpiRank];
  for ( int i = 0; i < numElements; i++ ){
    if (elements[i]->getElementType() == elem_type){
      elements[i]->getOutputConnectivity(&csr[ncsr], nodes);
      int n = nelems;
      elements[i]->addOutputCount(&nelems, &nodes, &ncsr);
      int c = elements[i]->getComponentNum();
      for ( ; n < nelems; n++ ){
        comp_nums[n] = c;
      }
    }
  }

  *component_nums = comp_nums;
  *_csr = csr;
  *_csr_range = csr_range;
  *_node_range = node_range;
}

/*
  Go through each element and get the output data for that element.

  The data is stored point-wise with each variable stored contiguously
  for each new point within the connectivity list. This stores the
  data at a point in memory indexed by data[node*nvals]. However,
  fewer than 'nvals' entries may be written in this call. The
  remaining data may be design variable entries that are computed
  below.

  elem_type: the element type to match
  out_type:  the output type 
  data:      the data array - nvals x the number of elements
  nvals:     the number of values to skip at each point
*/
void TACSAssembler::getOutputData( enum ElementType elem_type,
				   unsigned int out_type,
				   double * data, int nvals ){
  // Retrieve pointers to temporary storage
  TacsScalar *elemVars, *elemXpts;
  getDataPointers(elementData, &elemVars, NULL, NULL, NULL,
		  &elemXpts, NULL, NULL, NULL);
  
  int nelems = 0, nnodes = 0, ncsr = 0;
  for ( int i = 0; i < numElements; i++ ){
    if (elements[i]->getElementType() == elem_type){
      int ptr = elementNodeIndex[i];
      int len = elementNodeIndex[i+1] - ptr;
      const int *nodes = &elementTacsNodes[ptr];
      xptVec->getValues(len, nodes, elemXpts);
      varsVec->getValues(len, nodes, elemVars);

      elements[i]->getOutputData(out_type, &data[nvals*nnodes], nvals,
				 elemXpts, elemVars);
      elements[i]->addOutputCount(&nelems, &nnodes, &ncsr);
    }
  }  
}

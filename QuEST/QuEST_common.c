// Distributed under MIT licence. See https://github.com/aniabrown/QuEST_GPU/blob/master/LICENCE.txt for details

/** @file
 * Internal and API functions which are hardware-agnostic
 */

# include "QuEST.h"
# include "QuEST_internal.h"
# include "QuEST_precision.h"
# include "QuEST_validation.h"
# include "QuEST_ops.h"
# include "mt19937ar.h"

# define _BSD_SOURCE
# include <unistd.h>
# include <sys/types.h> 
# include <sys/time.h>
# include <sys/param.h>
# include <stdio.h>
# include <stdlib.h>


#ifdef __cplusplus
extern "C" {
#endif


REAL getVectorMagnitude(Vector vec) {
	return sqrt(vec.x*vec.x + vec.y*vec.y + vec.z*vec.z);
}

Vector getUnitVector(Vector vec) {
	
	REAL mag = getVectorMagnitude(vec);
	Vector unitVec = (Vector) {.x=vec.x/mag, .y=vec.y/mag, .z=vec.z/mag};
	return unitVec;
}

Complex getConjugateScalar(Complex scalar) {
	
	Complex conjScalar;
	conjScalar.real =   scalar.real;
	conjScalar.imag = - scalar.imag;
	return conjScalar;
}

ComplexMatrix2 getConjugateMatrix(ComplexMatrix2 matrix) {
	
	ComplexMatrix2 conjMatrix;
	conjMatrix.r0c0 = getConjugateScalar(matrix.r0c0);
	conjMatrix.r0c1 = getConjugateScalar(matrix.r0c1);
	conjMatrix.r1c0 = getConjugateScalar(matrix.r1c0);
	conjMatrix.r1c1 = getConjugateScalar(matrix.r1c1);
	return conjMatrix;
}

void shiftIndices(int* indices, int numIndices, int shift) {
	for (int j=0; j < numIndices; j++)
		indices[j] += shift;
}

unsigned long int hashString(char *str){
    unsigned long int hash = 5381;
    int c;

    while ((c = *str++))
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

    return hash;    
}

void seedQuESTDefault(){
    // init MT random number generator with three keys -- time, pid and a hash of hostname 
    // for the MPI version, it is ok that all procs will get the same seed as random numbers will only be 
    // used by the master process

    struct timeval  tv;
    gettimeofday(&tv, NULL);

    double time_in_mill = 
        (tv.tv_sec) * 1000 + (tv.tv_usec) / 1000 ; // convert tv_sec & tv_usec to millisecond

    unsigned long int pid = getpid();
    unsigned long int msecs = (unsigned long int) time_in_mill;
    char hostName[MAXHOSTNAMELEN+1];
    gethostname(hostName, sizeof(hostName));
    unsigned long int hostNameInt = hashString(hostName);

    unsigned long int key[3];
    key[0] = msecs; key[1] = pid; key[2] = hostNameInt;
    init_by_array(key, 3); 
}

/** 
 * numSeeds <= 64
 */
void seedQuEST(unsigned long int *seedArray, int numSeeds){
    // init MT random number generator with user defined list of seeds
    // for the MPI version, it is ok that all procs will get the same seed as random numbers will only be 
    // used by the master process
    init_by_array(seedArray, numSeeds); 
}

REAL statevec_getProbEl(QubitRegister qureg, long long int index){
    REAL real = statevec_getRealAmpEl(qureg, index);
    REAL imag = statevec_getImagAmpEl(qureg, index);
    return real*real + imag*imag;
}

void reportState(QubitRegister qureg){
	FILE *state;
	char filename[100];
	long long int index;
	sprintf(filename, "state_rank_%d.csv", qureg.chunkId);
	state = fopen(filename, "w");
	if (qureg.chunkId==0) fprintf(state, "real, imag\n");

	for(index=0; index<qureg.numAmpsPerChunk; index++){
		# if QuEST_PREC==1 || QuEST_PREC==2
		fprintf(state, "%.12f, %.12f\n", qureg.stateVec.real[index], qureg.stateVec.imag[index]);
		# elif QuEST_PREC == 4
		fprintf(state, "%.12Lf, %.12Lf\n", qureg.stateVec.real[index], qureg.stateVec.imag[index]);
		#endif
	}
	fclose(state);
}

void reportQubitRegisterParams(QubitRegister qureg){
    long long int numAmps = 1L << qureg.numQubitsInStateVec;
    long long int numAmpsPerRank = numAmps/qureg.numChunks;
    if (qureg.chunkId==0){
        printf("QUBITS:\n");
        printf("Number of qubits is %d.\n", qureg.numQubitsInStateVec);
        printf("Number of amps is %lld.\n", numAmps);
        printf("Number of amps per rank is %lld.\n", numAmpsPerRank);
    }
}

void statevec_phaseShift(QubitRegister qureg, const int targetQubit, REAL angle) {
	Complex term; 
	term.real = cos(angle); 
	term.imag = sin(angle);
	statevec_phaseShiftByTerm(qureg, targetQubit, term);
}

void statevec_sigmaZ(QubitRegister qureg, const int targetQubit) {
	Complex term; 
	term.real = -1;
	term.imag =  0;
    statevec_phaseShiftByTerm(qureg, targetQubit, term);
}

void statevec_sGate(QubitRegister qureg, const int targetQubit) {
	Complex term; 
	term.real = 0;
	term.imag = 1;
    statevec_phaseShiftByTerm(qureg, targetQubit, term);
} 

void statevec_tGate(QubitRegister qureg, const int targetQubit) {
	Complex term; 
	term.real = 1/sqrt(2);
	term.imag = 1/sqrt(2);
    statevec_phaseShiftByTerm(qureg, targetQubit, term);
}

void statevec_sGateConj(QubitRegister qureg, const int targetQubit) {
	Complex term; 
	term.real =  0;
	term.imag = -1;
    statevec_phaseShiftByTerm(qureg, targetQubit, term);
} 

void statevec_tGateConj(QubitRegister qureg, const int targetQubit) {
	Complex term; 
	term.real =  1/sqrt(2);
	term.imag = -1/sqrt(2);
    statevec_phaseShiftByTerm(qureg, targetQubit, term);
}

void statevec_rotateX(QubitRegister qureg, const int rotQubit, REAL angle){

    Vector unitAxis = {1, 0, 0};
    statevec_rotateAroundAxis(qureg, rotQubit, angle, unitAxis);
}

void statevec_rotateY(QubitRegister qureg, const int rotQubit, REAL angle){

    Vector unitAxis = {0, 1, 0};
    statevec_rotateAroundAxis(qureg, rotQubit, angle, unitAxis);
}

void statevec_rotateZ(QubitRegister qureg, const int rotQubit, REAL angle){

    Vector unitAxis = {0, 0, 1};
    statevec_rotateAroundAxis(qureg, rotQubit, angle, unitAxis);
}

void getAlphaBetaFromRotation(REAL angle, Vector axis, Complex* alpha, Complex* beta) {
	
    Vector unitAxis = getUnitVector(axis);
    alpha->real =   cos(angle/2.0);
    alpha->imag = - sin(angle/2.0)*unitAxis.z;	
    beta->real  =   sin(angle/2.0)*unitAxis.y;
    beta->imag  = - sin(angle/2.0)*unitAxis.x;
}

void statevec_rotateAroundAxis(QubitRegister qureg, const int rotQubit, REAL angle, Vector axis){

    Complex alpha, beta;
    getAlphaBetaFromRotation(angle, axis, &alpha, &beta);
    statevec_compactUnitary(qureg, rotQubit, alpha, beta);
}

void statevec_rotateAroundAxisConj(QubitRegister qureg, const int rotQubit, REAL angle, Vector axis){

    Complex alpha, beta;
    getAlphaBetaFromRotation(angle, axis, &alpha, &beta);
	alpha.imag *= -1; 
	beta.imag *= -1;
    statevec_compactUnitary(qureg, rotQubit, alpha, beta);
}

void statevec_controlledRotateAroundAxis(QubitRegister qureg, const int controlQubit, const int targetQubit, REAL angle, Vector axis){

    Complex alpha, beta;
    getAlphaBetaFromRotation(angle, axis, &alpha, &beta);
    statevec_controlledCompactUnitary(qureg, controlQubit, targetQubit, alpha, beta);
}

void statevec_controlledRotateAroundAxisConj(QubitRegister qureg, const int controlQubit, const int targetQubit, REAL angle, Vector axis){

    Complex alpha, beta;
    getAlphaBetaFromRotation(angle, axis, &alpha, &beta);
	alpha.imag *= -1; 
	beta.imag *= -1;
    statevec_controlledCompactUnitary(qureg, controlQubit, targetQubit, alpha, beta);
}

void statevec_controlledRotateX(QubitRegister qureg, const int controlQubit, const int targetQubit, REAL angle){

    Vector unitAxis = {1, 0, 0};
    statevec_controlledRotateAroundAxis(qureg, controlQubit, targetQubit, angle, unitAxis);
}

void statevec_controlledRotateY(QubitRegister qureg, const int controlQubit, const int targetQubit, REAL angle){

    Vector unitAxis = {0, 1, 0};
    statevec_controlledRotateAroundAxis(qureg, controlQubit, targetQubit, angle, unitAxis);
}

void statevec_controlledRotateZ(QubitRegister qureg, const int controlQubit, const int targetQubit, REAL angle){

    Vector unitAxis = {0, 0, 1};
    statevec_controlledRotateAroundAxis(qureg, controlQubit, targetQubit, angle, unitAxis);
}





#ifdef __cplusplus
}
#endif
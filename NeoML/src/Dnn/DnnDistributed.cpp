/* Copyright © 2017-2020 ABBYY Production LLC
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
	http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
--------------------------------------------------------------------------------------------------------------*/

#include <common.h>
#pragma hdrstop

#include <NeoMathEngine/NeoMathEngine.h>
#include <NeoML/Dnn/DnnDistributed.h>

namespace NeoML {

void CDistributedTraining::initialize( CArchive& archive, int count )
{
    NeoAssert( archive.IsLoading() );
    for( int i = 0; i < count; i++ ){
        rands.Add( new CRandom( 42 ) );
        cnns.Add( new CDnn( *rands[i], *mathEngines[i] ) );
        cnns[i]->SetInitializer( new CDnnDistributedInitializer( *rands[i], mathEngines[i], cnns[i]->GetInitializer() ) );
        archive.Serialize( *cnns[i] );
        archive.Seek( 0, static_cast<CBaseFile::TSeekPosition>( 0 ) );
    }
}

CDistributedTraining::CDistributedTraining( CArchive& archive, int count )
{
    mathEngines.SetSize( count );
    CreateDistributedCpuMathEngines( mathEngines.GetPtr(), count );
    initialize( archive, count );
}

CDistributedTraining::CDistributedTraining( CArchive& archive, const CArray<int>& cudaDevs )
{
    mathEngines.SetSize( cudaDevs.Size() );
    CreateDistributedCudaMathEngines( mathEngines.GetPtr(), cudaDevs.Size(), cudaDevs.GetPtr() );
    initialize( archive, cudaDevs.Size() );
}

CDistributedTraining::~CDistributedTraining()
{
    for( int i = 0; i < cnns.Size(); i++ ){
        delete cnns[i];
        delete rands[i];
        delete mathEngines[i];
    }
}

void CDistributedTraining::SetSolver( CArchive& archive )
{
    NeoAssert( archive.IsLoading() );
    long long startPos = archive.GetPosition();
    for( int i = 0; i < cnns.Size(); ++i ) {
        CPtr<CDnnSolver> newSolver = nullptr;
        SerializeSolver( archive, *cnns[i], newSolver );
        cnns[i]->SetSolver( newSolver );
        archive.Seek( startPos, CBaseFile::begin );
    }
}

void CDistributedTraining::SetLearningRate( float newRate )
{
    for( int i = 0; i < cnns.Size(); ++i ) {
        cnns[i]->GetSolver()->SetLearningRate( newRate );
    }
}

void CDistributedTraining::RunOnce( IDistributedDataset& data )
{
#ifdef NEOML_USE_OMP
    NEOML_OMP_NUM_THREADS( cnns.Size() )
    {
        const int thread = OmpGetThreadNum();
        try {
            data.SetInputBatch( *cnns[thread], thread );
            cnns[thread]->RunOnce();
        } catch( std::exception& e ) {
            if( errorMessage.IsEmpty() ) {
                errorMessage = e.what();
            }
            cnns[thread]->GetMathEngine().AbortDistributed();
        }
#ifdef NEOML_USE_FINEOBJ
        catch( CCheckException* e ) {
            if( errorMessage.IsEmpty() ) {
                errorMessage = e->MessageText().CreateString();
            }
            cnns[thread]->GetMathEngine().AbortDistributed();
            delete e;
        }
#endif
    }
    CheckArchitecture( errorMessage.IsEmpty(), "DistributedTraining", errorMessage );
#else
    ( void ) data;
    NeoAssert( false );
#endif
}

void CDistributedTraining::RunAndBackwardOnce( IDistributedDataset& data )
{
#ifdef NEOML_USE_OMP
    NEOML_OMP_NUM_THREADS( cnns.Size() )
    {
        const int thread = OmpGetThreadNum();
        try {
            data.SetInputBatch( *cnns[thread], thread );
            cnns[thread]->RunAndBackwardOnce();
        } catch( std::exception& e ) {
            if( errorMessage.IsEmpty() ) {
                errorMessage = e.what();
            }
            cnns[thread]->GetMathEngine().AbortDistributed();
        }
#ifdef NEOML_USE_FINEOBJ
        catch( CCheckException* e ) {
            if( errorMessage.IsEmpty() ) {
                errorMessage = e->MessageText().CreateString();
            }
            cnns[thread]->GetMathEngine().AbortDistributed();
            delete e;
        }
#endif
    }
    CheckArchitecture( errorMessage.IsEmpty(), "DistributedTraining", errorMessage );
#else
    ( void ) data;
    NeoAssert( false );
#endif
}

void CDistributedTraining::RunAndLearnOnce( IDistributedDataset& data )
{
#ifdef NEOML_USE_OMP
    NEOML_OMP_NUM_THREADS( cnns.Size() )
    {
        const int thread = OmpGetThreadNum();
        try {
            data.SetInputBatch( *cnns[thread], thread );
            cnns[thread]->RunAndLearnOnce();
        } catch( std::exception& e ) {
            if( errorMessage.IsEmpty() ){
                errorMessage = e.what();
            }
            cnns[thread]->GetMathEngine().AbortDistributed();
        }
#ifdef NEOML_USE_FINEOBJ
        catch( CCheckException* e ) {
            if( errorMessage.IsEmpty() ){
                errorMessage = e->MessageText().CreateString();
            }
            cnns[thread]->GetMathEngine().AbortDistributed();
            delete e;
        }
#endif
    }
    CheckArchitecture( errorMessage.IsEmpty(), "DistributedTraining", errorMessage );
#else
    (void)data;
    NeoAssert( false );
#endif
}

void CDistributedTraining::Train()
{
#ifdef NEOML_USE_OMP
    NEOML_OMP_NUM_THREADS( cnns.Size() )
    {
        const int thread = OmpGetThreadNum();
        try {
            cnns[thread]->GetSolver()->Train();
        } catch( std::exception& e ) {
            if( errorMessage.IsEmpty() ) {
                errorMessage = e.what();
            }
            cnns[thread]->GetMathEngine().AbortDistributed();
        }
#ifdef NEOML_USE_FINEOBJ
        catch( CCheckException* e ) {
            if( errorMessage.IsEmpty() ) {
                errorMessage = e->MessageText().CreateString();
            }
            cnns[thread]->GetMathEngine().AbortDistributed();
            delete e;
        }
#endif
    }
    CheckArchitecture( errorMessage.IsEmpty(), "DistributedTraining", errorMessage );
#else
    NeoAssert( false );
#endif
}

void CDistributedTraining::GetLastLoss( const CString& layerName, CArray<float>& losses )
{
    losses.SetSize( cnns.Size() );
    for( int i = 0; i < cnns.Size(); i++ ){
        CLossLayer* lossLayer = dynamic_cast<CLossLayer*>( cnns[i]->GetLayer( layerName ).Ptr() );
        if( lossLayer == nullptr ){
            CCtcLossLayer* ctc = dynamic_cast<CCtcLossLayer*>( cnns[i]->GetLayer( layerName ).Ptr() );
            if( ctc != nullptr ) {
                losses[i] = ctc->GetLastLoss();
            } else {
                losses[i] = CheckCast<CCrfLossLayer>( cnns[i]->GetLayer( layerName ) )->GetLastLoss();
            }
        } else {
            losses[i] = lossLayer->GetLastLoss();
        }
    }
}

void CDistributedTraining::GetLastBlob( const CString& layerName, CObjectArray<CDnnBlob>& blobs )
{
    blobs.SetSize( cnns.Size() );
    for( int i = 0; i < cnns.Size(); ++i ) {
        blobs[i] = CheckCast<CSinkLayer>( cnns[i]->GetLayer( layerName ) )->GetBlob();
    }
}

void CDistributedTraining::Serialize( CArchive& archive )
{
    NeoAssert( archive.IsStoring() );
    archive.Serialize( *cnns[0] );
}

} // namespace NeoML
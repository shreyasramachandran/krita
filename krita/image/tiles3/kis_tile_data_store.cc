/*
 *  Copyright (c) 2009 Dmitry Kazakov <dimula73@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */


#include <stdio.h>
#include "kis_tile_data_store.h"
#include "kis_debug.h"

#include "kis_tile_data_store_iterators.h"

//#define DEBUG_PRECLONE

#ifdef DEBUG_PRECLONE
#define DEBUG_PRECLONE_ACTION(action, oldTD, newTD) \
    printf("!!! %s:\t\t\t  0x%X -> 0x%X    \t\t!!!\n",  \
           action, (quintptr)oldTD, (quintptr) newTD)
#define DEBUG_FREE_ACTION(td)                   \
    printf("Tile data free'd \t(0x%X)\n", td)
#else
#define DEBUG_PRECLONE_ACTION(action, oldTD, newTD)
#define DEBUG_FREE_ACTION(td)
#endif


KisTileDataStore globalTileDataStore;


KisTileDataStore::KisTileDataStore()
    : m_pooler(this),
      m_swapper(this),
      m_listRWLock(QReadWriteLock::Recursive),
      m_numTiles(0)
{
    m_clockIterator = m_tileDataList.end();
    m_pooler.start();
    m_swapper.start();
}

KisTileDataStore::~KisTileDataStore()
{
    m_pooler.terminatePooler();
    m_swapper.terminateSwapper();
    freeRegisteredTiles();
}

void KisTileDataStore::registerTileData(KisTileData *td)
{
    QWriteLocker lock(&m_listRWLock);
    td->m_listIterator = m_tileDataList.insert(m_tileDataList.end(), td);
    m_numTiles++;
}

void KisTileDataStore::unregisterTileData(KisTileData *td)
{
    QWriteLocker lock(&m_listRWLock);
    KisTileDataListIterator tempIterator = td->m_listIterator;

    if(m_clockIterator == tempIterator) {
        m_clockIterator = tempIterator + 1;
    }

    td->m_listIterator = m_tileDataList.end();
    m_tileDataList.erase(tempIterator);
    m_numTiles--;
}

void KisTileDataStore::freeRegisteredTiles()
{
    QWriteLocker lock(&m_listRWLock);

    KisTileDataListIterator iter = m_tileDataList.begin();

    while(iter != m_tileDataList.end()) {
        delete *iter;
        iter = m_tileDataList.erase(iter);
    }
}

KisTileData *KisTileDataStore::allocTileData(qint32 pixelSize, const quint8 *defPixel)
{
    m_swapper.checkFreeMemory();

    KisTileData *td = new KisTileData(pixelSize, defPixel, this);
    registerTileData(td);
    return td;
}

KisTileData *KisTileDataStore::duplicateTileData(KisTileData *rhs)
{
    KisTileData *td = 0;

    if (rhs->m_clonesStack.pop(td)) {
        DEBUG_PRECLONE_ACTION("+ Pre-clone HIT", rhs, td);
    } else {
        blockSwapping(rhs);
        td = new KisTileData(*rhs);
        unblockSwapping(rhs);
        DEBUG_PRECLONE_ACTION("- Pre-clone #MISS#", rhs, td);
    }

    registerTileData(td);
    return td;
}

void KisTileDataStore::freeTileData(KisTileData *td)
{
    Q_ASSERT(td->m_store == this);

    DEBUG_FREE_ACTION(td);

    td->m_swapLock.lockForWrite();
    if(!td->data()) {
        m_swappedStore.forgetTileData(td);
    }
    unregisterTileData(td);
    td->m_swapLock.unlock();

    delete td;
}

void KisTileDataStore::ensureTileDataLoaded(KisTileData *td)
{
//    qDebug() << "#### SWAP MISS! ####" << td << ppVar(td->mementoed()) << ppVar(td->age()) << ppVar(td->numUsers());

    td->m_swapLock.lockForRead();

    while(!td->data()) {
        td->m_swapLock.unlock();

        td->m_swapLock.lockForWrite();

        if(!td->data()) {
            m_swappedStore.swapInTileData(td);
        }

        td->m_swapLock.unlock();

        /**
         * <-- In theory, livelock is possible here...
         */

        td->m_swapLock.lockForRead();
    }
}

bool KisTileDataStore::trySwapTileData(KisTileData *td)
{
    bool result = false;
    if(!td->m_swapLock.tryLockForWrite()) return result;

    if(td->data()) {
        m_swappedStore.swapOutTileData(td);
        result = true;
    }
    td->m_swapLock.unlock();

    return result;
}

KisTileDataStoreIterator* KisTileDataStore::beginIteration()
{
    m_listRWLock.lockForRead();
    return new KisTileDataStoreIterator(m_tileDataList);
}
void KisTileDataStore::endIteration(KisTileDataStoreIterator* iterator)
{
    delete iterator;
    m_listRWLock.unlock();
}

KisTileDataStoreReverseIterator* KisTileDataStore::beginReverseIteration()
{
    m_listRWLock.lockForRead();
    return new KisTileDataStoreReverseIterator(m_tileDataList);
}
void KisTileDataStore::endIteration(KisTileDataStoreReverseIterator* iterator)
{
    delete iterator;
    m_listRWLock.unlock();
}

KisTileDataStoreClockIterator* KisTileDataStore::beginClockIteration()
{
    m_listRWLock.lockForRead();
    return new KisTileDataStoreClockIterator(m_tileDataList, m_clockIterator);
}
void KisTileDataStore::endIteration(KisTileDataStoreClockIterator* iterator)
{
    m_clockIterator = iterator->getFinalPosition();
    delete iterator;
    m_listRWLock.unlock();
}

void KisTileDataStore::debugPrintList()
{
    KisTileData *item;
    foreach(item, m_tileDataList) {
        dbgTiles << "-------------------------\n"
                 << "TileData:\t\t\t" << item
                 << "\n  refCount:\t" << item->m_refCount;
    }
}

void KisTileDataStore::debugSwapAll()
{
    QWriteLocker lock(&m_listRWLock);

    qint32 numSwapped = 0;
    KisTileData *item;
    foreach(item, m_tileDataList) {
        if(trySwapTileData(item)) {
            numSwapped++;
        }
    }

    qDebug() << "Swapped out tile data:" << numSwapped;
    qDebug() << "Total tile data:" << m_numTiles;

    m_swappedStore.debugStatistics();
}

/**
 *    Copyright (C) 2010-2015 MongoDB Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include <string>
#include <vector>

#include "mongo/s/catalog/catalog_manager.h"
#include "mongo/s/query/cluster_cursor_manager.h"
#include "mongo/stdx/memory.h"
#include "mongo/stdx/mutex.h"
#include "mongo/util/concurrency/rwlock.h"

namespace mongo {

class BSONObj;
class CatalogCache;
class DBConfig;
class OperationContext;
class SettingsType;
class ShardRegistry;
template <typename T>
class StatusWith;


/**
 * Holds the global sharding context. Single instance exists for a running server. Exists on
 * both MongoD and MongoS.
 */
class Grid {
public:
    class CatalogManagerGuard;

    Grid();

    /**
     * Called at startup time so the global sharding services can be set. This method must be called
     * once and once only for the lifetime of the service.
     *
     * NOTE: Unit-tests are allowed to call it more than once, provided they reset the object's
     *       state using clearForUnitTests.
     */
    void init(std::unique_ptr<CatalogManager> catalogManager,
              std::unique_ptr<ShardRegistry> shardRegistry,
              std::unique_ptr<ClusterCursorManager> cursorManager);

    /**
     * Implicitly creates the specified database as non-sharded.
     */
    StatusWith<std::shared_ptr<DBConfig>> implicitCreateDb(OperationContext* txn,
                                                           const std::string& dbName);

    /**
     * @return true if shards and config servers are allowed to use 'localhost' in address
     */
    bool allowLocalHost() const;

    /**
     * @param whether to allow shards and config servers to use 'localhost' in address
     */
    void setAllowLocalHost(bool allow);

    /**
     * Returns true if the balancer should be running. Caller is responsible
     * for making sure settings has the balancer key.
     */
    bool shouldBalance(const SettingsType& balancerSettings) const;

    /**
     * Returns true if the config server settings indicate that the balancer should be active.
     */
    bool getConfigShouldBalance(OperationContext* txn) const;

    Grid::CatalogManagerGuard catalogManager(OperationContext* txn);
    Grid::CatalogManagerGuard catalogManager();  // TODO(spencer): remove

    CatalogCache* catalogCache() {
        return _catalogCache.get();
    }
    ShardRegistry* shardRegistry() {
        return _shardRegistry.get();
    }

    ClusterCursorManager* getCursorManager() {
        return _cursorManager.get();
    }

    /**
     * Compares desiredMode against _catalogManager->getMode().  Returns Status::OK() if they match,
     * returns ErrorCodes::IncompatibleCatalogManger if desiredMode is CSRS and the current mode is
     * SCCC. If desiredMode is SCCC and current mode is CSRS returns InvalidOperation as we do not
     * support downgrade.
     * TODO(spencer): Support downgrade.
     */
    Status checkIfCatalogNeedsSwapping(CatalogManager::ConfigServerMode desiredMode);

    /**
     * Clears the grid object so that it can be reused between test executions. This will not
     * be necessary if grid is hanging off the global ServiceContext and each test gets its
     * own service context.
     *
     * NOTE: Do not use this outside of unit-tests.
     */
    void clearForUnitTests();

private:
    std::unique_ptr<CatalogCache> _catalogCache;
    std::unique_ptr<ShardRegistry> _shardRegistry;
    std::unique_ptr<ClusterCursorManager> _cursorManager;

    // can 'localhost' be used in shard addresses?
    bool _allowLocalShard;

    // Concurrency control around access the _catalogManager is as follows.
    // In order to read _catalogManager, one must either lock _catalogManagerLock in shared mode
    // or hold _catalogManagerMutex.  In order to change _catalogManager, one must hold
    // both _catalogManagerLock in exclusive mode and _catalogManagerMutex.

    // Guards access to _catalogManager
    stdx::mutex _catalogManagerMutex;

    /**
     * Protects access to _catalogManager.
     * All normal accessors of the current CatalogManager will go through Grid::CatalogManagerGuard,
     * which always takes the lock in shared mode.  In order to swap the active catalog manager,
     * the lock must be held in exclusive mode.
     * TODO(SERVER-19875) Use a new lock manager resource for this instead.
     */
    RWLock _catalogManagerLock;

    // Current active catalog manager
    std::unique_ptr<CatalogManager> _catalogManager;
};

/**
 * Guard object that protects access to the current active CatalogManager to enable switching the
 * active catalog manager at runtime.
 * Note: Never contstruct a CatalogManagerGuard directly, they should only be given out by calling
 * grid.catalogManager().
 */
class Grid::CatalogManagerGuard {
    MONGO_DISALLOW_COPYING(CatalogManagerGuard);

public:
    CatalogManagerGuard(OperationContext* txn, Grid* grid);
    ~CatalogManagerGuard();

    CatalogManager* operator->() const;

    explicit operator bool() const;

    CatalogManager* get() const;

#if defined(_MSC_VER) && _MSC_VER < 1900
    CatalogManagerGuard(CatalogManagerGuard&& other) : _grid(other._grid) {}

    CatalogManagerGuard& operator=(CatalogManagerGuard&& other) {
        _grid = other._grid;
        return *this;
    }
#else
    CatalogManagerGuard(CatalogManagerGuard&& other) = default;
    CatalogManagerGuard& operator=(CatalogManagerGuard&& other) = default;
#endif

private:
    Grid* _grid;
};

extern Grid grid;

}  // namespace mongo

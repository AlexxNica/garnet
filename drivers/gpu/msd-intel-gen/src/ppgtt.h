// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PPGTT_H
#define PPGTT_H

#include <memory>
#include <vector>
#include "address_space.h"
#include "magma_util/address_space_allocator.h"
#include "magma_util/register_io.h"
#include "platform_buffer.h"

using gen_pde_t = uint64_t;
using gen_pdpe_t = uint64_t;
using gen_pml4e_t = uint64_t;

static inline gen_pde_t gen_pde_encode(uint64_t bus_addr)
{
    return bus_addr | PAGE_RW | PAGE_PRESENT;
}

static inline gen_pdpe_t gen_pdpe_encode(uint64_t bus_addr)
{
    return bus_addr | PAGE_RW | PAGE_PRESENT;
}

static inline gen_pml4e_t gen_pml4_encode(uint64_t bus_addr)
{
    return bus_addr | PAGE_RW | PAGE_PRESENT;
}

class PerProcessGtt : public AddressSpace {
public:
    static std::unique_ptr<PerProcessGtt> Create(std::shared_ptr<GpuMappingCache> cache);

    uint64_t Size() const override { return kSize; }

    static void InitPrivatePat(RegisterIo* reg_io);

    // AddressSpace overrides
    bool Alloc(size_t size, uint8_t align_pow2, uint64_t* addr_out) override;
    bool Free(uint64_t addr) override;

    bool Clear(uint64_t addr) override;
    bool Insert(uint64_t addr, magma::PlatformBuffer* buffer, uint64_t offset, uint64_t length,
                CachingType caching_type) override;

    uint64_t get_pml4_bus_addr() { return pml4_table_->bus_addr(); }

    // Legacy 48-bit ppgtt = 512 PDPs; each PDP has 512 PDs; each PD handles 1GB
    // (512 * 512 * 4096)
    static constexpr uint64_t kPml4Entries = 512;

    static constexpr uint64_t kPageDirectoryPtrShift = 9;
    static constexpr uint64_t kPageDirectoryPtrEntries = 1 << kPageDirectoryPtrShift;
    static constexpr uint64_t kPageDirectoryPtrMask = kPageDirectoryPtrEntries - 1;

    static constexpr uint64_t kPageDirectoryShift = 9;
    static constexpr uint64_t kPageDirectoryEntries = 1 << kPageDirectoryShift;
    static constexpr uint64_t kPageDirectoryMask = kPageDirectoryEntries - 1;

    static constexpr uint64_t kPageTableShift = 9;
    static constexpr uint64_t kPageTableEntries = 1 << kPageTableShift;
    static constexpr uint64_t kPageTableMask = kPageTableEntries - 1;

    // These structures are overlayed onto mapped buffers
    struct PageTableGpu {
        gen_pte_t entry[kPageTableEntries];
    };

    struct PageDirectoryTableGpu {
        gen_pde_t entry[kPageDirectoryEntries];
    };

    struct PageDirectoryPtrTableGpu {
        gen_pdpe_t entry[kPageDirectoryPtrEntries];
    };

    struct Pml4TableGpu {
        gen_pml4e_t entry[kPml4Entries];
    };

    class Page {
    public:
        bool Init()
        {
            buffer_ = magma::PlatformBuffer::Create(PAGE_SIZE, "ppgtt table");
            if (!buffer_)
                return DRETF(false, "couldn't create buffer");

            if (!buffer_->PinPages(0, 1))
                return DRETF(false, "failed to pin pages");

            if (!buffer_->MapCpu(&mapping_))
                return DRETF(false, "failed to map cpu");

            if (!buffer_->MapPageRangeBus(0, 1, &bus_addr_))
                return DRETF(false, "failed to map page range bus");

            return true;
        }

        void* mapping() { return mapping_; }

        uint64_t bus_addr() { return bus_addr_; }

    private:
        std::unique_ptr<magma::PlatformBuffer> buffer_;
        void* mapping_;
        uint64_t bus_addr_;
    };

    class PageTable : public Page {
    public:
        gen_pte_t* page_table_entry(uint32_t page_index)
        {
            DASSERT(page_index < kPageTableEntries);
            return &reinterpret_cast<PageTableGpu*>(mapping())->entry[page_index];
        }

        static std::unique_ptr<PageTable> Create()
        {
            auto page_table = std::unique_ptr<PageTable>(new PageTable());
            if (!page_table->Init())
                return DRETP(nullptr, "page table init failed");
            return page_table;
        }

    private:
        PageTable() {}
    };

    class PageDirectory : public Page {
    public:
        PageDirectoryTableGpu* page_directory_table_gpu()
        {
            return reinterpret_cast<PageDirectoryTableGpu*>(mapping());
        }

        PageTable* page_table(uint32_t index)
        {
            DASSERT(index < kPageDirectoryEntries);
            if (!page_tables_[index]) {
                page_tables_[index] = PageTable::Create();
                if (!page_tables_[index])
                    return DRETP(nullptr, "couldn't create page table");
                page_directory_table_gpu()->entry[index] =
                    gen_pde_encode(page_tables_[index]->bus_addr());
            }
            return page_tables_[index].get();
        }

        gen_pte_t* page_table_entry(uint32_t page_directory_index, uint32_t page_table_index)
        {
            auto table = page_table(page_directory_index);
            if (!table)
                return nullptr;
            return table->page_table_entry(page_table_index);
        }

        static std::unique_ptr<PageDirectory> Create()
        {
            auto dir = std::unique_ptr<PageDirectory>(new PageDirectory());
            if (!dir->Init())
                return DRETP(nullptr, "init failed");
            return dir;
        }

    private:
        PageDirectory() : page_tables_(kPageDirectoryEntries) {}

        std::vector<std::unique_ptr<PageTable>> page_tables_;
    };

    class PageDirectoryPtrTable : public Page {
    public:
        PageDirectoryPtrTableGpu* page_directory_ptr_table_gpu()
        {
            return reinterpret_cast<PageDirectoryPtrTableGpu*>(mapping());
        }

        PageDirectory* page_directory(uint32_t index)
        {
            DASSERT(index < kPageDirectoryPtrEntries);
            if (!page_directories_[index]) {
                page_directories_[index] = PageDirectory::Create();
                if (!page_directories_[index])
                    return DRETP(nullptr, "couldn't create page directory");
                page_directory_ptr_table_gpu()->entry[index] =
                    gen_pdpe_encode(page_directories_[index]->bus_addr());
            }
            return page_directories_[index].get();
        }

        gen_pte_t* page_table_entry(uint32_t page_directory_ptr_index,
                                    uint32_t page_directory_index, uint32_t page_table_index)
        {
            auto dir = page_directory(page_directory_ptr_index);
            if (!dir)
                return nullptr;
            return dir->page_table_entry(page_directory_index, page_table_index);
        }

        static std::unique_ptr<PageDirectoryPtrTable> Create()
        {
            auto table = std::unique_ptr<PageDirectoryPtrTable>(new PageDirectoryPtrTable());
            if (!table->Init())
                return DRETP(nullptr, "init failed");
            return table;
        }

    private:
        PageDirectoryPtrTable() : page_directories_(kPageDirectoryPtrEntries) {}

        std::vector<std::unique_ptr<PageDirectory>> page_directories_;
    };

    class Pml4Table : public Page {
    public:
        Pml4TableGpu* pml4_table_gpu() { return reinterpret_cast<Pml4TableGpu*>(mapping()); }

        PageDirectoryPtrTable* page_directory_ptr(uint32_t index)
        {
            DASSERT(index < kPml4Entries);
            if (!directory_ptrs_[index]) {
                directory_ptrs_[index] = PageDirectoryPtrTable::Create();
                if (!directory_ptrs_[index])
                    return DRETP(nullptr, "couldn't create page directory ptr table");
                pml4_table_gpu()->entry[index] =
                    gen_pml4_encode(directory_ptrs_[index]->bus_addr());
            }
            return directory_ptrs_[index].get();
        }

        PageDirectory* page_directory(uint32_t pml4_index, uint32_t page_directory_ptr_index)
        {
            auto dir_ptr = page_directory_ptr(pml4_index);
            if (!dir_ptr)
                return nullptr;
            return dir_ptr->page_directory(page_directory_ptr_index);
        }

        uint64_t scratch_page_bus_addr() { return scratch_page_bus_addr_; }

        static std::unique_ptr<Pml4Table>
        Create(std::unique_ptr<magma::PlatformBuffer> scratch_page, uint64_t scratch_bus_addr)
        {
            auto table = std::unique_ptr<Pml4Table>(
                new Pml4Table(std::move(scratch_page), scratch_bus_addr));
            if (!table->Init())
                return DRETP(nullptr, "init failed");
            return table;
        }

    private:
        Pml4Table(std::unique_ptr<magma::PlatformBuffer> scratch_page,
                  uint64_t scratch_page_bus_addr)
            : directory_ptrs_(kPml4Entries + 1), scratch_page_buffer_(std::move(scratch_page)),
              scratch_page_bus_addr_(scratch_page_bus_addr)
        {
        }

        std::vector<std::unique_ptr<PageDirectoryPtrTable>> directory_ptrs_;
        std::shared_ptr<magma::PlatformBuffer> scratch_page_buffer_;
        uint64_t scratch_page_bus_addr_;
    };

    Pml4Table* pml4_table() { return pml4_table_.get(); }

private:
    PerProcessGtt(std::unique_ptr<Pml4Table> pml4_table, std::shared_ptr<GpuMappingCache> cache);

    static constexpr uint64_t kSize = kPml4Entries * kPageDirectoryPtrEntries *
                                      kPageDirectoryEntries * kPageTableEntries * PAGE_SIZE;

    static constexpr uint32_t kOverfetchPageCount = 1;
    static constexpr uint32_t kGuardPageCount = 8;

    static_assert(kSize == 1ull << 48, "ppgtt size calculation");

    bool Init();
    bool Clear(uint64_t start, uint64_t length);

    bool initialized_ = false;
    std::unique_ptr<Pml4Table> pml4_table_;
    std::unique_ptr<magma::AddressSpaceAllocator> allocator_;

    // For testing
    friend class TestPerProcessGtt;
    gen_pte_t get_pte(gpu_addr_t gpu_addr);
};

#endif // PPGTT_H

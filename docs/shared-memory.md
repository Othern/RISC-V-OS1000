# Shared Memory

This kernel currently provides a small teaching-oriented shared memory API.
It is page based, fixed size, and managed entirely by the kernel.

## Model

- The kernel keeps up to `SHM_MAX` shared memory objects.
- Each object owns one physical page.
- A user process obtains an object with `shm_get(key)`.
- A user process maps the object with `shm_attach(id)`.
- Multiple mappings can point to the same physical page.
- `shm_detach(addr)` removes one process mapping and decrements the object's reference count.
- When the reference count reaches zero, the physical page is released.

The mapping shape is:

```text
process A VA -> shared PA
process B VA -> same shared PA
```

The first implementation uses the per-process shared memory VA window starting
at `SHM_USER_BASE` (`0x01700000`). Each process can hold up to `PROC_SHM_MAX`
shared mappings.

## Syscalls

- `SYS_SHM_GET`
- `SYS_SHM_ATTACH`
- `SYS_SHM_DETACH`
- `SYS_SHM_DUMP`

User wrappers are declared in `include/user.h`:

```c
int shm_get(int key);
void *shm_attach(int id);
int shm_detach(void *addr);
void shm_dump(void);
```

## Shell Tests

Run the kernel and use:

```text
shm
shmtest
```

`shm` prints the current kernel shared memory table.

`shmtest` obtains key `100`, attaches the same object twice, writes through the
first virtual address, reads through the second virtual address, and prints
`shmtest: PASS` when both mappings observe the same data. It also dumps the
shared memory table before and after detach so the reference count can be
observed.

Expected success shape:

```text
shmtest: id=1
shmtest: va_a=0x1700000 va_b=0x1701000
shmtest: read via va_b: shared-memory-ok
shmtest: PASS
shared memory objects:
  id=1 key=100 pa=0x... size=4096 refcount=2
shared memory objects:
```

## Limitations

- One page per object.
- No permissions beyond user read/write mapping.
- No copy-from-user/copy-to-user validation beyond the existing syscall model.
- No named files, mmap, lazy allocation, or page fault handling.
- No cache aging, ownership model, or inter-process synchronization primitive.

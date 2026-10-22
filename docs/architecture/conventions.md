# Coding Conventions

Conventions are concrete expectations for project code. They cover choices that
are too specific to be principles and too small to be patterns.

## Language and build

- Use C++23.
- Do not use exceptions or RTTI. The project builds with both disabled.
- Keep project code free of compiler warnings. The build treats warnings as
  errors for project targets.

## Errors and invariants

- Use `Result` for recoverable failures.
- Use assertions and the project's verification helpers for programming errors
  and violated invariants.

## Switch statements

Avoid `default` in switches over enums. Handle each enumerator explicitly so
compiler warnings identify missing cases when the enum changes.

Use `default` when a fallback is intentional, such as switching over an
open-ended set of integer values.

## Allocation and containers

- Heap allocation is acceptable during program startup.
- Small allocations are also acceptable when they are short-lived and do not
  happen repeatedly, such as once per frame or update.
- Prefer a small number of bulk allocations over many long-lived small
  allocations.
- Prefer `std::string::append()` over `operator+` for concatenation. It accepts
  `std::string_view` directly and can reuse existing capacity, avoiding
  unnecessary temporary strings.
- Use `std::vector` for dynamically sized collections when contiguous storage is
  appropriate.
- Make an effort to determine a vector's eventual size and call `reserve()`
  before adding elements so the vector does not repeatedly grow.
- Reuse the capacity of temporary vectors in frequently run code. When
  practical, keep the vector with the system that uses it and call `clear()`
  between uses instead of repeatedly creating and destroying it.
- Avoid non-contiguous containers such as `std::list` and `std::map` unless their
  specific behavior is needed and a contiguous representation would not be a
  good fit.
- Look for a contiguous alternative before choosing a non-contiguous container.
  Prefer `std::vector` over `std::list`, and consider a sorted vector with binary
  search instead of `std::map`.

Why:

Many long-lived small allocations add allocator overhead, increase memory
fragmentation, and spread related data across memory. Repeated allocation in a
frequently run path also adds bookkeeping and can make frame time less
predictable. Occasional temporary allocations are less concerning because
their cost does not accumulate and the memory is quickly returned to the
allocator. Bulk contiguous storage uses less overhead and gives the CPU better
memory locality.

`std::vector::clear()` destroys the elements and resets the vector's size to
zero, but it does not release the allocation. The vector's capacity remains
available for the next use.

## Parameters stored by value

When a function or constructor stores a non-trivial movable value, such as a
`std::string` or `std::vector`, pass the parameter by value and move it into
storage. Pass small, trivially copyable values by value and assign them
normally.

```cpp
class Thing
{
public:
    explicit Thing(std::string name)
        : m_Name(std::move(name))
    {
    }

private:
    std::string m_Name;
};
```

Why:

- With an lvalue: copy-construct the parameter, then move-construct the member.
- With an rvalue: move-construct the parameter, then move-construct the member.
- With a temporary: construction of the parameter may be elided, followed by
  moving the parameter into the member.
- `std::move()` casts the named parameter to an rvalue so the member's move
  constructor can be used.
- A `const` value or `const` reference cannot be moved into the member, so the
  member must be copied instead.

When a function only reads a non-trivial value and does not store it, usually
pass it by `const` reference. Small non-owning views are passed by value as
described below.

## Non-owning views

- Use a non-owning view like `std::span` or `std::string_view` when a function
  borrows existing data and only needs it for the duration of the call.
- Pass small view types like `std::span` and `std::string_view` by value, not by
  reference.
- Do not store a view unless the API clearly requires its source to outlive the
  object storing it.
- When an object needs to keep the data, accept an appropriate owning type and
  move it into the object.

Why:

A view provides access without taking ownership or copying the underlying data.
Small views are cheap to copy, so passing them by reference adds unnecessary
indirection. Views do not extend the lifetime of the data they refer to.
Storing an owning type keeps the object's lifetime independent of the caller's
storage.

## Copy and move behavior

- Let ordinary value types use compiler-generated copy and move operations.
- Explicitly declare copy and move operations for types that own resources or
  have lifetime or address constraints.
- Delete any operation that does not have valid semantics for the type.
- Only default move operations when all member variables behave correctly when
  moved.

Why:

Copying or moving a resource-owning object can transfer ownership, duplicate
ownership, or leave internal references pointing at the wrong object. Declaring
these operations explicitly makes those semantics deliberate and prevents them
from changing accidentally when members change.

A default move copies raw pointers and does not clear them in the moved-from
object. When a pointer represents ownership or must be cleared after a move,
implement the move operations explicitly. The same care is needed for internal
pointers or views that refer to another member's storage.

## Non-owned dependencies

When storing a non-owned dependency as a pointer, accept it as a reference and
store its address. Accepting it as a reference prevents the possibility of
passing `nullptr`.

The dependency must outlive the object that stores its address. Accept a pointer
instead of a reference only when `nullptr` is a valid argument.

## WebGPU and threads

Only access WebGPU objects on the main thread. This includes creating and
releasing objects, calling their methods, and reading properties through getters.

In browser builds, WebGPU objects belong to a JavaScript context and are not
shared with worker threads. For example, even calling `GetWidth()` on a texture
from a worker can fail.

Give workers the plain data they need instead of WebGPU objects.

## Style

- Format source code using the root `.clang-format` file.
- Follow the naming used by nearby project code.

## Comments and API documentation

- Use `///` for documentation comments.
- Document contracts and non-obvious behavior instead of restating declarations.
- Do not require comments for every public method. Omit comments when the declaration already
  communicates the complete contract.
- Document private methods when they have non-obvious behavior, constraints, side effects, ordering
  requirements, or invariants.
- Use `@brief`, `@param`, `@return`, and similar tags only when the tag adds information that is not
  already clear from the declaration and surrounding documentation.

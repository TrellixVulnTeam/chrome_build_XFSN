// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_INSTALLER_ZUCCHINI_DISASSEMBLER_H_
#define CHROME_INSTALLER_ZUCCHINI_DISASSEMBLER_H_

#include <memory>
#include <string>
#include <vector>

#include "base/optional.h"
#include "chrome/installer/zucchini/buffer_view.h"
#include "chrome/installer/zucchini/image_utils.h"

namespace zucchini {

class Disassembler;

// Interface for extracting References through member function GetNext().
// This is used by Disassemblers to extract references from an image file.
// Typycally, a Reader lazily extracts values and does not hold any storage.
class ReferenceReader {
 public:
  virtual ~ReferenceReader() = default;

  // Returns the next available Reference, or nullopt_t if exhausted.
  // Extracted References must be ordered by their location in the image.
  virtual base::Optional<Reference> GetNext() = 0;
};

// Interface for writing References through member function
// PutNext(reference). This is used by Disassemblers to write new References
// in the image file.
class ReferenceWriter {
 public:
  virtual ~ReferenceWriter() = default;

  // Writes |reference| in the underlying image file. This operation always
  // succeeds.
  virtual void PutNext(Reference reference) = 0;
};

// A ReferenceGroup is associated with a specific |type| and has convenience
// methods to obtain readers and writers for that type. A ReferenceGroup does
// not store references; it is a lightweight class that communicates with the
// disassembler to operate on them.
class ReferenceGroup {
 public:
  // Member function pointer used to obtain a ReferenceReader.
  using ReaderFactory = std::unique_ptr<ReferenceReader> (
      Disassembler::*)(offset_t lower, offset_t upper);

  // Member function pointer used to obtain a ReferenceWriter.
  using WriterFactory = std::unique_ptr<ReferenceWriter> (Disassembler::*)(
      MutableBufferView image);

  ReferenceGroup() = default;

  // RefinedGeneratorFactory and RefinedReceptorFactory don't have to be
  // identical to GeneratorFactory and ReceptorFactory, but they must be
  // convertible. As a result, they can be pointer to member function of a
  // derived Disassembler.
  template <class RefinedReaderFactory, class RefinedWriterFactory>
  ReferenceGroup(ReferenceTypeTraits traits,
                 RefinedReaderFactory reader_factory,
                 RefinedWriterFactory writer_factory)
      : traits_(traits),
        reader_factory_(static_cast<ReaderFactory>(reader_factory)),
        writer_factory_(static_cast<WriterFactory>(writer_factory)) {}

  // Returns a reader for all references in the binary.
  // Invalidates any other writer or reader previously obtained for |disasm|.
  std::unique_ptr<ReferenceReader> GetReader(Disassembler* disasm) const;

  // Returns a reader for references whose bytes are entirely contained in
  // |[lower, upper)|.
  // Invalidates any other writer or reader previously obtained for |disasm|.
  std::unique_ptr<ReferenceReader> GetReader(offset_t lower,
                                             offset_t upper,
                                             Disassembler* disasm) const;

  // Returns a writer for references in |image|, assuming that |image| was the
  // same one initially parsed by |disasm|.
  // Invalidates any other writer or reader previously obtained for |disasm|.
  std::unique_ptr<ReferenceWriter> GetWriter(MutableBufferView image,
                                             Disassembler* disasm) const;

  // Returns traits describing the reference type.
  const ReferenceTypeTraits& traits() const { return traits_; }

  // Shorthand for traits().width.
  offset_t width() const { return traits().width; }

  // Shorthand for traits().type_tag.
  TypeTag type_tag() const { return traits().type_tag; }

  // Shorthand for traits().pool_tag.
  PoolTag pool_tag() const { return traits().pool_tag; }

 private:
  ReferenceTypeTraits traits_;
  ReaderFactory reader_factory_ = nullptr;
  WriterFactory writer_factory_ = nullptr;
};

// A Disassembler is used to encapsulate architecture specific operations, to:
// - Describe types of references found in the architecture using traits.
// - Extract references contained in an image file.
// - Correct target for some references.
class Disassembler {
 public:
  virtual ~Disassembler();

  // Returns the type of executable handled by the Disassembler.
  virtual ExecutableType GetExeType() const = 0;

  // Returns a more detailed description of the executable type.
  virtual std::string GetExeTypeString() const = 0;

  // Creates and returns a vector that contains all groups of references.
  // Groups must be aggregated by pool.
  virtual std::vector<ReferenceGroup> GetReferenceGroups() const = 0;

  ConstBufferView GetImage() const { return image_; }
  size_t size() const { return image_.size(); }

 protected:
  Disassembler();

  // Parses |image| and initializes internal states. Returns true on success.
  // This must be called once and before any other operation.
  virtual bool Parse(ConstBufferView image) = 0;

  // Raw image data. After Parse(), a Disassembler should shrink this to contain
  // only the portion containing the executable file it recognizes.
  ConstBufferView image_;
};

}  // namespace zucchini

#endif  // CHROME_INSTALLER_ZUCCHINI_DISASSEMBLER_H_

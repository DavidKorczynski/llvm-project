//===- ArmSMEToLLVM.cpp - Convert ArmSME to LLVM dialect ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements lowering of ArmSME operations to LLVM intrinsics.
//
//===----------------------------------------------------------------------===//

#include "mlir/Conversion/ArmSMEToLLVM/ArmSMEToLLVM.h"

#include "mlir/Conversion/LLVMCommon/ConversionTarget.h"
#include "mlir/Conversion/LLVMCommon/Pattern.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ArmSME/IR/ArmSME.h"
#include "mlir/Dialect/ArmSME/Utils/Utils.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/Vector/IR/VectorOps.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"

namespace mlir {
#define GEN_PASS_DEF_CONVERTARMSMETOLLVM
#include "mlir/Conversion/Passes.h.inc"
} // namespace mlir

using namespace mlir;

namespace {

/// Helper to create an arm_sme.intr.ld1*.(horiz|vert)' intrinsic.
static Operation *createLoadTileSliceIntrinsic(
    RewriterBase &rewriter, Location loc, arm_sme::ArmSMETileType type,
    arm_sme::TileSliceLayout layout, Value maskOp, Value ptr,
    IntegerAttr tileId, Value tileSliceI32) {
  if (layout == arm_sme::TileSliceLayout::Horizontal) {
    switch (type) {
    case arm_sme::ArmSMETileType::ZAB:
      return rewriter.create<arm_sme::aarch64_sme_ld1b_horiz>(
          loc, maskOp, ptr, tileId, tileSliceI32);
    case arm_sme::ArmSMETileType::ZAH:
      return rewriter.create<arm_sme::aarch64_sme_ld1h_horiz>(
          loc, maskOp, ptr, tileId, tileSliceI32);
    case arm_sme::ArmSMETileType::ZAS:
      return rewriter.create<arm_sme::aarch64_sme_ld1w_horiz>(
          loc, maskOp, ptr, tileId, tileSliceI32);
    case arm_sme::ArmSMETileType::ZAD:
      return rewriter.create<arm_sme::aarch64_sme_ld1d_horiz>(
          loc, maskOp, ptr, tileId, tileSliceI32);
    case arm_sme::ArmSMETileType::ZAQ:
      return rewriter.create<arm_sme::aarch64_sme_ld1q_horiz>(
          loc, maskOp, ptr, tileId, tileSliceI32);
    }
  } else {
    switch (type) {
    case arm_sme::ArmSMETileType::ZAB:
      return rewriter.create<arm_sme::aarch64_sme_ld1b_vert>(
          loc, maskOp, ptr, tileId, tileSliceI32);
    case arm_sme::ArmSMETileType::ZAH:
      return rewriter.create<arm_sme::aarch64_sme_ld1h_vert>(
          loc, maskOp, ptr, tileId, tileSliceI32);
    case arm_sme::ArmSMETileType::ZAS:
      return rewriter.create<arm_sme::aarch64_sme_ld1w_vert>(
          loc, maskOp, ptr, tileId, tileSliceI32);
    case arm_sme::ArmSMETileType::ZAD:
      return rewriter.create<arm_sme::aarch64_sme_ld1d_vert>(
          loc, maskOp, ptr, tileId, tileSliceI32);
    case arm_sme::ArmSMETileType::ZAQ:
      return rewriter.create<arm_sme::aarch64_sme_ld1q_vert>(
          loc, maskOp, ptr, tileId, tileSliceI32);
      break;
    }
  }
}

/// Helper to create an arm_sme.intr.st1*.(horiz|vert)' intrinsic.
static Operation *createStoreTileSliceIntrinsic(
    RewriterBase &rewriter, Location loc, arm_sme::ArmSMETileType type,
    arm_sme::TileSliceLayout layout, Value maskOp, Value ptr,
    IntegerAttr tileId, Value tileSliceI32) {
  if (layout == arm_sme::TileSliceLayout::Horizontal) {
    switch (type) {
    case arm_sme::ArmSMETileType::ZAB:
      return rewriter.create<arm_sme::aarch64_sme_st1b_horiz>(
          loc, maskOp, ptr, tileId, tileSliceI32);
    case arm_sme::ArmSMETileType::ZAH:
      return rewriter.create<arm_sme::aarch64_sme_st1h_horiz>(
          loc, maskOp, ptr, tileId, tileSliceI32);
    case arm_sme::ArmSMETileType::ZAS:
      return rewriter.create<arm_sme::aarch64_sme_st1w_horiz>(
          loc, maskOp, ptr, tileId, tileSliceI32);
    case arm_sme::ArmSMETileType::ZAD:
      return rewriter.create<arm_sme::aarch64_sme_st1d_horiz>(
          loc, maskOp, ptr, tileId, tileSliceI32);
    case arm_sme::ArmSMETileType::ZAQ:
      return rewriter.create<arm_sme::aarch64_sme_st1q_horiz>(
          loc, maskOp, ptr, tileId, tileSliceI32);
    }
  } else {
    switch (type) {
    case arm_sme::ArmSMETileType::ZAB:
      return rewriter.create<arm_sme::aarch64_sme_st1b_vert>(
          loc, maskOp, ptr, tileId, tileSliceI32);
    case arm_sme::ArmSMETileType::ZAH:
      return rewriter.create<arm_sme::aarch64_sme_st1h_vert>(
          loc, maskOp, ptr, tileId, tileSliceI32);
    case arm_sme::ArmSMETileType::ZAS:
      return rewriter.create<arm_sme::aarch64_sme_st1w_vert>(
          loc, maskOp, ptr, tileId, tileSliceI32);
    case arm_sme::ArmSMETileType::ZAD:
      return rewriter.create<arm_sme::aarch64_sme_st1d_vert>(
          loc, maskOp, ptr, tileId, tileSliceI32);
    case arm_sme::ArmSMETileType::ZAQ:
      return rewriter.create<arm_sme::aarch64_sme_st1q_vert>(
          loc, maskOp, ptr, tileId, tileSliceI32);
    }
  }
}

IntegerAttr getTileIdOrError(arm_sme::ArmSMETileOpInterface op) {
  auto tileId = op.getTileId();
  if (!tileId)
    op.emitOpError(
        "expected tile ID to be allocated before conversion to LLVM");
  return tileId;
}

struct GetTileConversion : public ConvertOpToLLVMPattern<arm_sme::GetTileOp> {
  using ConvertOpToLLVMPattern<arm_sme::GetTileOp>::ConvertOpToLLVMPattern;

  LogicalResult
  matchAndRewrite(arm_sme::GetTileOp getTile, OpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    rewriter.replaceOpWithNewOp<arm_sme::MaterializeSSATileOp>(
        getTile, getTile.getTileType());
    return success();
  }
};

/// Lower 'arm_sme.zero' to SME intrinsics.
///
///  BEFORE:
///  ```mlir
///     %v = arm_sme.zero {tile_id = 0 : i32} : vector<[4]x[4]xi32>
///  ```
///
///  AFTER:
///  ```mlir
///     "arm_sme.intr.zero"() <{tile_mask = 17 : i32}> : () -> ()
///     %v = arm_sme.materialize_ssa_tile : vector<[4]x[4]xi32>
///  ```
///
///  The 'arm_sme.materialize_ssa_tile' (which models the return) will fold away
///  once all ArmSME ops have been converted to LLVM intrinsics.
struct ZeroOpConversion : public ConvertOpToLLVMPattern<arm_sme::ZeroOp> {
  using ConvertOpToLLVMPattern<arm_sme::ZeroOp>::ConvertOpToLLVMPattern;

  LogicalResult
  matchAndRewrite(arm_sme::ZeroOp zero, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto loc = zero.getLoc();

    auto tileId = getTileIdOrError(zero);
    if (!tileId)
      return failure();

    // Get the base mask for tile based on the element size.
    // The base mask is just the mask to zero the first tile (of a size).
    // These masks are derived from:
    // https://developer.arm.com/documentation/ddi0602/2022-06/SME-Instructions/ZERO--Zero-a-list-of-64-bit-element-ZA-tiles-
    arm_sme::ArmSMETileType tileType = *zero.getAllocatedTileType();
    auto baseMaskForSize = [&] {
      switch (tileType) {
      case arm_sme::ArmSMETileType::ZAB:
        // Zeroing the 8-bit ZA0.B tile is equivalent to zeroing all eight
        // 64-bit element tiles named ZA0.D to ZA7.D.
        return 0b1111'1111;
      case arm_sme::ArmSMETileType::ZAH:
        // Zeroing the 16-bit ZA0.H tile is equivalent to zeroing 64-bit
        // element tiles named ZA0.D, ZA2.D, ZA4.D, and ZA6.D. Shift this left
        // once for ZA1.H.
        return 0b0101'0101;
      case arm_sme::ArmSMETileType::ZAS:
        // Zeroing the 32-bit ZA0.S tile is equivalent to zeroing 64-bit
        // element tiles named ZA0.D and ZA4.D.
        // Shift left by 1, 2, or 3 respectively for ZA1.S, ZA2.S, ZA3.S.
        return 0b0001'0001;
      case arm_sme::ArmSMETileType::ZAD:
        // Zeroing one of the a 64-bit tiles ZA0.D to ZA7.D just requires
        // setting the bit for that tile.
        return 0b0000'0001;
      default:
        llvm_unreachable("bad element size");
      }
    }();

    // The actual mask is just the base mask shifted by the tile ID.
    // This will be folded to a constant after tile allocation.
    //
    // The shift is just derived from the layout of the tiles, and that the tile
    // ID is the index of the tile. For example, looking at the 32-bit ZAx.S
    // tiles:
    //
    // ZA0.S = ZA0.D and ZA4.D
    //  * Tile ID -> 0
    //  * Mask    -> 00010001 = (00010001 << 0)
    // ZA1.S = ZA1.D and ZA5.D
    //  * Tile ID -> 1
    //  * Mask    -> 00100010 = (00010001 << 1)
    // ZA2.S = ZA2.D and ZA6.D
    //  * Tile ID -> 2
    //  * Mask    -> 01000100 = (00010001 << 2)
    // ZA3.S = ZA3.D and ZA7.D
    //  * Tile ID -> 3
    //  * Mask    -> 10001000 = (00010001 << 3)
    //
    // This holds for all tile sizes.
    int32_t zeroMask = baseMaskForSize << int32_t(tileId.getInt());
    rewriter.create<arm_sme::aarch64_sme_zero>(
        loc, rewriter.getI32IntegerAttr(zeroMask));

    // Create a placeholder op to preserve dataflow.
    rewriter.replaceOpWithNewOp<arm_sme::MaterializeSSATileOp>(
        zero, zero.getVectorType());

    return success();
  }
};

/// Lower `arm_sme.load_tile_slice` to SME intrinsics.
struct LoadTileSliceConversion
    : public ConvertOpToLLVMPattern<arm_sme::LoadTileSliceOp> {
  using ConvertOpToLLVMPattern<
      arm_sme::LoadTileSliceOp>::ConvertOpToLLVMPattern;

  LogicalResult
  matchAndRewrite(arm_sme::LoadTileSliceOp loadTileSliceOp,
                  arm_sme::LoadTileSliceOp::Adaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto loc = loadTileSliceOp.getLoc();
    auto tileId = getTileIdOrError(loadTileSliceOp);
    if (!tileId)
      return failure();

    Value ptr = this->getStridedElementPtr(loc, loadTileSliceOp.getMemRefType(),
                                           adaptor.getBase(),
                                           adaptor.getIndices(), rewriter);

    auto tileSlice = loadTileSliceOp.getTileSliceIndex();

    // Cast tile slice to i32 for intrinsic.
    auto tileSliceI32 = rewriter.create<arith::IndexCastUIOp>(
        loc, rewriter.getI32Type(), tileSlice);

    // Create all active predicate mask.
    auto maskOp = loadTileSliceOp.getMask();

    auto tileVectorType = loadTileSliceOp.getVectorType();
    arm_sme::ArmSMETileType tileType = *arm_sme::getSMETileType(tileVectorType);
    arm_sme::TileSliceLayout layout = loadTileSliceOp.getLayout();

    // Create 'arm_sme.intr.ld1*.(horiz|vert)' intrinsic to load ZA tile slice.
    createLoadTileSliceIntrinsic(rewriter, loc, tileType, layout, maskOp, ptr,
                                 tileId, tileSliceI32);

    // The load intrinsics have no result, replace 'arm_sme.tile_load' with
    // the input tile to preserve dataflow.
    rewriter.replaceOp(loadTileSliceOp, loadTileSliceOp.getTile());

    return success();
  }
};

/// Lower for `arm_sme.store_tile_slice` to SME intrinsics.
struct StoreTileSliceConversion
    : public ConvertOpToLLVMPattern<arm_sme::StoreTileSliceOp> {
  using ConvertOpToLLVMPattern<
      arm_sme::StoreTileSliceOp>::ConvertOpToLLVMPattern;

  LogicalResult
  matchAndRewrite(arm_sme::StoreTileSliceOp storeTileSliceOp,
                  arm_sme::StoreTileSliceOp::Adaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto loc = storeTileSliceOp.getLoc();
    auto tileVectorType = storeTileSliceOp.getVectorType();

    auto tileId = getTileIdOrError(storeTileSliceOp);
    if (!tileId)
      return failure();

    // Create 'arm_sme.intr.st1*.horiz' intrinsic to store ZA tile slice.
    Value ptr = this->getStridedElementPtr(
        loc, storeTileSliceOp.getMemRefType(), adaptor.getBase(),
        adaptor.getIndices(), rewriter);

    auto tileSlice = storeTileSliceOp.getTileSliceIndex();

    // Cast tile slice to i32 for intrinsic.
    auto tileSliceI32 = rewriter.create<arith::IndexCastUIOp>(
        loc, rewriter.getI32Type(), tileSlice);

    auto maskOp = storeTileSliceOp.getMask();

    arm_sme::TileSliceLayout layout = storeTileSliceOp.getLayout();
    arm_sme::ArmSMETileType tileType = *arm_sme::getSMETileType(tileVectorType);

    rewriter.replaceOp(storeTileSliceOp,
                       createStoreTileSliceIntrinsic(rewriter, loc, tileType,
                                                     layout, maskOp, ptr,
                                                     tileId, tileSliceI32));

    return success();
  }
};

/// Lower `arm_sme.move_vector_to_tile_slice` to SME intrinsics.
struct MoveVectorToTileSliceConversion
    : public ConvertOpToLLVMPattern<arm_sme::MoveVectorToTileSliceOp> {
  using ConvertOpToLLVMPattern<
      arm_sme::MoveVectorToTileSliceOp>::ConvertOpToLLVMPattern;

  LogicalResult
  matchAndRewrite(arm_sme::MoveVectorToTileSliceOp moveVectorToTileSliceOp,
                  arm_sme::MoveVectorToTileSliceOp::Adaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto loc = moveVectorToTileSliceOp.getLoc();
    auto tileType = moveVectorToTileSliceOp.getTileType();

    auto tileId = getTileIdOrError(moveVectorToTileSliceOp);
    if (!tileId)
      return failure();

    auto tileSlice = moveVectorToTileSliceOp.getTileSliceIndex();

    // Cast tile slice from index to i32 for intrinsic.
    auto tileSliceI32 = rewriter.create<arith::IndexCastUIOp>(
        loc, rewriter.getI32Type(), tileSlice);

    // Create all active predicate mask.
    auto one = rewriter.create<arith::ConstantOp>(
        loc, rewriter.getI1Type(),
        rewriter.getIntegerAttr(rewriter.getI1Type(), 1));
    auto predTy = VectorType::get(tileType.getShape()[0], rewriter.getI1Type(),
                                  /*scalableDims=*/{true});
    auto allActiveMask = rewriter.create<vector::SplatOp>(loc, predTy, one);

    // Create 'arm_sme.intr.write.(horiz|vert)' to write vector to tile slice.
    switch (moveVectorToTileSliceOp.getLayout()) {
    case arm_sme::TileSliceLayout::Horizontal:
      rewriter.create<arm_sme::aarch64_sme_write_horiz>(
          loc, tileId, tileSliceI32, allActiveMask,
          moveVectorToTileSliceOp.getVector());
      break;
    case arm_sme::TileSliceLayout::Vertical:
      rewriter.create<arm_sme::aarch64_sme_write_vert>(
          loc, tileId, tileSliceI32, allActiveMask,
          moveVectorToTileSliceOp.getVector());
      break;
    }

    // Intrinsic has no result, replace 'arm_sme.move_vector_to_tile_slice' with
    // the input tile to preserve dataflow.
    rewriter.replaceOp(moveVectorToTileSliceOp,
                       moveVectorToTileSliceOp.getTile());

    return success();
  }
};

/// Lower `arm_sme.move_tile_slice_to_vector` to SME intrinsics.
struct MoveTileSliceToVectorConversion
    : public ConvertOpToLLVMPattern<arm_sme::MoveTileSliceToVectorOp> {
  using ConvertOpToLLVMPattern<
      arm_sme::MoveTileSliceToVectorOp>::ConvertOpToLLVMPattern;

  LogicalResult
  matchAndRewrite(arm_sme::MoveTileSliceToVectorOp moveTileSliceToVector,
                  OpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto loc = moveTileSliceToVector.getLoc();
    auto sliceType = moveTileSliceToVector.getSliceType();
    auto sliceIndex = moveTileSliceToVector.getTileSliceIndex();

    auto tileId = getTileIdOrError(moveTileSliceToVector);
    if (!tileId)
      return failure();

    // Create an 'all true' predicate for the tile slice.
    auto predicateType = sliceType.cloneWith({}, rewriter.getI1Type());
    auto allTruePredicate = rewriter.create<arith::ConstantOp>(
        loc, DenseElementsAttr::get(predicateType, true));

    // Zero destination/fallback for tile slice extraction.
    auto zeroVector = rewriter.create<arith::ConstantOp>(
        loc, sliceType, rewriter.getZeroAttr(sliceType));

    // Cast tile slice from index to i32 for intrinsic.
    auto sliceIndexI32 = rewriter.create<arith::IndexCastOp>(
        loc, rewriter.getI32Type(), sliceIndex);

    // Create 'arm_sme.intr.read.(horiz|vert)' to extract the tile slice.
    switch (moveTileSliceToVector.getLayout()) {
    case arm_sme::TileSliceLayout::Horizontal:
      rewriter.replaceOpWithNewOp<arm_sme::aarch64_sme_read_horiz>(
          moveTileSliceToVector, sliceType, zeroVector, allTruePredicate,
          tileId, sliceIndexI32);
      break;
    case arm_sme::TileSliceLayout::Vertical:
      rewriter.replaceOpWithNewOp<arm_sme::aarch64_sme_read_vert>(
          moveTileSliceToVector, sliceType, zeroVector, allTruePredicate,
          tileId, sliceIndexI32);
      break;
    }

    return success();
  }
};

/// Lower `arm_sme.outerproduct` to SME MOPA intrinsics.
///
/// Example:
///
///   %0 = arm_sme.outerproduct %lhs, %rhs acc(%acc)
///     : vector<[4]xf32>, vector<[4]xf32>
///
/// is converted to:
///
///   "arm_sme.intr.mopa"(%ptrue_s, %ptrue_s, %lhs, %rhs) <{tile_id = 0 : i32}>
///     : (vector<[4]xi1>, vector<[4]xi1>, vector<[4]xf32>,
///        vector<[4]xf32>) -> ()
///
/// Currently only supports FMOPA and BFMOPA (non-widening).
struct OuterProductOpConversion
    : public ConvertOpToLLVMPattern<arm_sme::OuterProductOp> {
  using ConvertOpToLLVMPattern<arm_sme::OuterProductOp>::ConvertOpToLLVMPattern;

  LogicalResult
  matchAndRewrite(arm_sme::OuterProductOp outerProductOp,
                  arm_sme::OuterProductOp::Adaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto tileId = getTileIdOrError(outerProductOp);
    if (!tileId)
      return failure();

    auto isSupportedType = [](VectorType vectorType) {
      // TODO: the FP outer product instruction variants are predicated on
      // different features [1]:
      //
      // * FMOPA (non-widening)
      //   * half-precision   - +sme2p1,+sme-f16f16
      //   * single-precision - +sme
      //   * double-precision - +sme-f64f64
      // * BFMOPA
      //   * half-precision   - +sme2p1,+b16b16
      //
      // It should be possible to control lowering based on target features.
      // [1]
      // https://developer.arm.com/downloads/-/exploration-tools/feature-names-for-a-profile
      if ((vectorType.getRank() != 2) || !vectorType.allDimsScalable())
        return false;

      auto elementType = vectorType.getElementType();

      if (!elementType.isF16() && !elementType.isBF16() &&
          !elementType.isF32() && !elementType.isF64())
        return false;

      unsigned minNumElts = arm_sme::MinStreamingVectorLengthInBits /
                            vectorType.getElementTypeBitWidth();
      if (vectorType.getShape() != ArrayRef<int64_t>({minNumElts, minNumElts}))
        return false;

      return true;
    };

    // TODO: Support CombiningKind::Sub for outer products.
    if (outerProductOp.getKind() != arm_sme::CombiningKind::Add)
      return outerProductOp.emitError("unsupported kind");

    auto resultVectorType = outerProductOp.getResultType();
    if (!isSupportedType(resultVectorType))
      return outerProductOp.emitError("unsupported type");

    auto loc = outerProductOp.getLoc();

    Value acc = outerProductOp.getAcc();
    if (!acc)
      // Initalize accumulator with zero.
      acc = outerProductOp.createOpAndForwardTileId<arm_sme::ZeroOp>(
          rewriter, loc, resultVectorType);

    Value lhsMask = outerProductOp.getLhsMask();
    Value rhsMask = outerProductOp.getRhsMask();

    if (!lhsMask || !rhsMask) {
      auto predTy =
          outerProductOp.getLhsType().cloneWith({}, rewriter.getI1Type());
      Value allActiveMask = rewriter.create<arith::ConstantOp>(
          loc, DenseElementsAttr::get(predTy, true));
      lhsMask = allActiveMask;
      rhsMask = allActiveMask;
    }

    // Create 'arm_sme.intr.mopa' outer product intrinsic.
    rewriter.create<arm_sme::aarch64_sme_mopa>(loc, tileId, lhsMask, rhsMask,
                                               outerProductOp.getLhs(),
                                               outerProductOp.getRhs());

    // The outerproduct intrinsics have no result, replace
    // 'arm_sme.outerproduct' with the input tile to preserve dataflow.
    rewriter.replaceOp(outerProductOp, acc);

    return success();
  }
};

/// Lower `arm_sme.streaming_vl` to SME CNTS intrinsics.
///
/// Example:
///
///   %0 = arm_sme.streaming_vl <half>
///
/// is converted to:
///
///   %cnt = "arm_sme.intr.cntsh"() : () -> i64
///   %0 = arith.index_cast %cnt : i64 to index
///
struct StreamingVLOpConversion
    : public ConvertOpToLLVMPattern<arm_sme::StreamingVLOp> {
  using ConvertOpToLLVMPattern<arm_sme::StreamingVLOp>::ConvertOpToLLVMPattern;

  LogicalResult
  matchAndRewrite(arm_sme::StreamingVLOp streamingVlOp,
                  arm_sme::StreamingVLOp::Adaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto loc = streamingVlOp.getLoc();
    auto i64Type = rewriter.getI64Type();
    auto *intrOp = [&]() -> Operation * {
      switch (streamingVlOp.getTypeSize()) {
      case arm_sme::TypeSize::Byte:
        return rewriter.create<arm_sme::aarch64_sme_cntsb>(loc, i64Type);
      case arm_sme::TypeSize::Half:
        return rewriter.create<arm_sme::aarch64_sme_cntsh>(loc, i64Type);
      case arm_sme::TypeSize::Word:
        return rewriter.create<arm_sme::aarch64_sme_cntsw>(loc, i64Type);
      case arm_sme::TypeSize::Double:
        return rewriter.create<arm_sme::aarch64_sme_cntsd>(loc, i64Type);
      }
    }();
    rewriter.replaceOpWithNewOp<arith::IndexCastOp>(
        streamingVlOp, rewriter.getIndexType(), intrOp->getResult(0));
    return success();
  }
};

} // namespace

namespace {

struct ConvertArmSMEToLLVMPass
    : public impl::ConvertArmSMEToLLVMBase<ConvertArmSMEToLLVMPass> {
  void runOnOperation() override {
    LLVMConversionTarget target(getContext());
    RewritePatternSet patterns(&getContext());
    LLVMTypeConverter converter(&getContext());
    configureArmSMEToLLVMConversionLegality(target);
    populateArmSMEToLLVMConversionPatterns(converter, patterns);

    if (failed(applyPartialConversion(getOperation(), target,
                                      std::move(patterns))))
      signalPassFailure();
  }
};

} // namespace

void mlir::configureArmSMEToLLVMConversionLegality(ConversionTarget &target) {
  target.addIllegalDialect<arm_sme::ArmSMEDialect>();
  target.addLegalOp<
      arm_sme::MaterializeSSATileOp, arm_sme::aarch64_sme_zero,
      arm_sme::aarch64_sme_str, arm_sme::aarch64_sme_ld1b_horiz,
      arm_sme::aarch64_sme_ld1h_horiz, arm_sme::aarch64_sme_ld1w_horiz,
      arm_sme::aarch64_sme_ld1d_horiz, arm_sme::aarch64_sme_ld1q_horiz,
      arm_sme::aarch64_sme_st1b_horiz, arm_sme::aarch64_sme_st1h_horiz,
      arm_sme::aarch64_sme_st1w_horiz, arm_sme::aarch64_sme_st1d_horiz,
      arm_sme::aarch64_sme_st1q_horiz, arm_sme::aarch64_sme_ld1b_vert,
      arm_sme::aarch64_sme_ld1h_vert, arm_sme::aarch64_sme_ld1w_vert,
      arm_sme::aarch64_sme_ld1d_vert, arm_sme::aarch64_sme_ld1q_vert,
      arm_sme::aarch64_sme_st1b_vert, arm_sme::aarch64_sme_st1h_vert,
      arm_sme::aarch64_sme_st1w_vert, arm_sme::aarch64_sme_st1d_vert,
      arm_sme::aarch64_sme_st1q_vert, arm_sme::aarch64_sme_read_horiz,
      arm_sme::aarch64_sme_read_vert, arm_sme::aarch64_sme_write_horiz,
      arm_sme::aarch64_sme_write_vert, arm_sme::aarch64_sme_mopa,
      arm_sme::aarch64_sme_cntsb, arm_sme::aarch64_sme_cntsh,
      arm_sme::aarch64_sme_cntsw, arm_sme::aarch64_sme_cntsd>();
  target.addLegalDialect<arith::ArithDialect>();
  target.addLegalOp<UnrealizedConversionCastOp>();
}

void mlir::populateArmSMEToLLVMConversionPatterns(LLVMTypeConverter &converter,
                                                  RewritePatternSet &patterns) {
  converter.addConversion([&](VectorType type) -> std::optional<Type> {
    // There's no LLVM type for SME tiles, but after lowering to intrinsics all
    // SME vector types should be eliminated.
    if (arm_sme::isValidSMETileVectorType(type))
      return type;
    return std::nullopt;
  });

  patterns.add<LoadTileSliceConversion, MoveTileSliceToVectorConversion,
               MoveVectorToTileSliceConversion, StoreTileSliceConversion,
               OuterProductOpConversion, ZeroOpConversion, GetTileConversion,
               StreamingVLOpConversion>(converter);
}

std::unique_ptr<Pass> mlir::createConvertArmSMEToLLVMPass() {
  return std::make_unique<ConvertArmSMEToLLVMPass>();
}

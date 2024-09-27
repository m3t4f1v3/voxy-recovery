import amulet
from amulet.api.block import Block

# level = amulet.load_level("/tmp/world")
level = amulet.load_level("empty")

game_version = ("java", (1, 20, 1))  # the version that we want the block data in.

block, block_entity = level.get_version_block(
    0,  # x location
    0,  # y location
    0,  # z location
    "minecraft:overworld",  # dimension
    game_version,
)

if isinstance(block, Block):
    # Check that what we have is actually a block.
    # There are some edge cases such as item frames where the returned value might not be a Block.
    print(block)
    # Block(minecraft:air)

block = Block("minecraft", "stone")
level.set_version_block(
    0,  # x location
    70,  # y location
    0,  # z location
    "minecraft:overworld",  # dimension
    game_version,
    block,
)

# level.save()
level.close()
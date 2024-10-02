import sys
import json
import amulet
from amulet.api.block import Block

from amulet_nbt import StringTag

def process_block_data(data, level, game_version):
    try:
        # Extract x, y, z, block name, and properties from the JSON input
        x = data.get("x")
        y = data.get("y")
        z = data.get("z")
        block_name = data.get("Name")
        properties = data.get("Properties", {})  # Get properties, default to empty dict
        # print(properties)
        processed_properties = {}
        for value in properties:
            processed_properties[value] = StringTag(properties[value])
            # match properties[value][1]: # the type
            #     case "string":
            #         processed_properties[value] = StringTag(properties[value][0])
            #     case "string":
            #         processed_properties[value] = StringTag(properties[value][0])
        # print(processed_properties)
        if x is None or y is None or z is None or block_name is None:
            print(f"Invalid data: {data}")
            return

        # Create a Block object with the specified block name and properties
        namespace, base_name = block_name.split(":")
        block = Block(namespace, base_name, processed_properties)

        # Set the block in the world at the specified coordinates
        level.set_version_block(
            x,
            y,
            z,
            "minecraft:overworld",  # Dimension
            game_version,
            block,
        )
        # print(f"Set block '{block_name}' with properties {properties} at ({x}, {y}, {z})")
    
    except Exception as e:
        print(f"Error processing block data {data}: {e}")

def main():
    game_version = ("java", (1, 20, 1))  # Specify the game version
    level_path = "empty"  # Path to the Minecraft world
    level = amulet.load_level(level_path)

    try:
        # Read line-buffered JSON input from stdin
        for line in sys.stdin:
            try:
                # Parse the JSON string into a dictionary
                block_data = json.loads(line.strip())
                process_block_data(block_data, level, game_version)
            except json.JSONDecodeError:
                print(f"Invalid JSON input: {line.strip()}")

    finally:
        # Make sure to close the level and save any changes
        level.save()  # Uncomment if saving is desired
        level.close()

if __name__ == "__main__":
    main()

from setuptools import setup, find_packages


setup(
    name="ueagentforge-mcp",
    version="0.5.0",
    description="MCP server bridge for the UEAgentForge Unreal Editor plugin",
    package_dir={"": ".."},
    packages=find_packages(where="..", include=["mcp_server"]),
    py_modules=["ueagentforge_client"],
    include_package_data=True,
    package_data={
        "mcp_server": [
            "knowledge_base/*.md",
            "*.json",
            "requirements.txt",
        ],
    },
    install_requires=[
        "mcp[cli]>=1.0,<2.0",
        "requests>=2.31,<3.0",
    ],
    python_requires=">=3.9",
    entry_points={
        "console_scripts": [
            "ueagentforge-mcp=mcp_server.agentforge_mcp_server:main",
        ],
    },
)

#pragma once

namespace hyperagent::mcp::schema {

inline constexpr const char* tools_json = R"schema([
  {
    "name": "list_projects",
    "description": "List the projects in the parent directory that can become peer agents. Launches nothing.",
    "inputSchema": {
      "type": "object",
      "properties": {
        "all": {
          "type": "boolean",
          "description": "include directories without a project marker"
        }
      },
      "additionalProperties": false
    }
  },
  {
    "name": "start_projects",
    "description": "Start Claude Code peer sessions for the named projects through airc. Only the names given are started. Returns ready state per peer.",
    "inputSchema": {
      "type": "object",
      "properties": {
        "names": {
          "type": "array",
          "items": {
            "type": "string"
          },
          "minItems": 1
        },
        "concurrency": {
          "type": "number"
        },
        "harness": {
          "type": "string",
          "description": "claude, codex or freebuff; defaults to the project's remembered harness"
        },
        "resume": {
          "type": "boolean",
          "description": "resume each project's latest session when it is not running"
        },
        "fork": {
          "type": "boolean",
          "description": "fork the resumed session instead of reusing its id"
        },
        "trust": {
          "type": "boolean",
          "description": "answer the folder-trust dialog (default true)"
        }
      },
      "required": [
        "names"
      ],
      "additionalProperties": false
    }
  },
  {
    "name": "instruct",
    "description": "Prompt one peer session and wait for its turn. Starts the peer if needed. Returns the peer's text plus status; a non-turn-end status includes the pane tail.",
    "inputSchema": {
      "type": "object",
      "properties": {
        "name": {
          "type": "string"
        },
        "prompt": {
          "type": "string"
        },
        "timeout_s": {
          "type": "number"
        },
        "idle_exit_s": {
          "type": "number"
        },
        "tools": {
          "type": "boolean"
        },
        "wait": {
          "type": "boolean",
          "description": "true blocks until the turn ends; false (default) returns a turn_id at once"
        },
        "harness": {
          "type": "string",
          "description": "claude, codex or freebuff; defaults to the project's remembered harness"
        },
        "resume": {
          "type": "boolean",
          "description": "resume the project's latest session when the peer is not running"
        },
        "fork": {
          "type": "boolean",
          "description": "fork the resumed session instead of reusing its id"
        },
        "trust": {
          "type": "boolean"
        }
      },
      "required": [
        "name",
        "prompt"
      ],
      "additionalProperties": false
    }
  },
  {
    "name": "instruct_many",
    "description": "Prompt many peer sessions concurrently and collect their turns. Each job names a peer and a prompt; a job without cwd uses that project's directory.",
    "inputSchema": {
      "type": "object",
      "properties": {
        "jobs": {
          "type": "array",
          "minItems": 1,
          "items": {
            "type": "object",
            "properties": {
              "name": {
                "type": "string"
              },
              "prompt": {
                "type": "string"
              },
              "harness": {
                "type": "string"
              },
              "timeout_s": {
                "type": "number"
              },
              "idle_exit_s": {
                "type": "number"
              }
            },
            "required": [
              "name",
              "prompt"
            ]
          }
        },
        "timeout_s": {
          "type": "number"
        },
        "idle_exit_s": {
          "type": "number"
        },
        "tools": {
          "type": "boolean"
        },
        "wait": {
          "type": "boolean",
          "description": "true blocks until every turn ends; false (default) returns turn_ids at once"
        },
        "harness": {
          "type": "string"
        },
        "resume": {
          "type": "boolean"
        },
        "fork": {
          "type": "boolean"
        },
        "trust": {
          "type": "boolean"
        }
      },
      "required": [
        "jobs"
      ],
      "additionalProperties": false
    }
  },
  {
    "name": "turn_result",
    "description": "Read a background turn started by instruct or instruct_many with wait=false.",
    "inputSchema": {
      "type": "object",
      "properties": {
        "turn_id": {
          "type": "string"
        },
        "wait_s": {
          "type": "number"
        }
      },
      "required": [
        "turn_id"
      ],
      "additionalProperties": false
    }
  },
  {
    "name": "resume_project",
    "description": "Resume a project's conversation in claude, codex or freebuff. With no project it resumes the last project used; with no session_id it resumes that project's newest session. A specific project, harness or session_id can be given. Starts the airc server and peer as needed and optionally sends a prompt, returning the resumed session and the turn.",
    "inputSchema": {
      "type": "object",
      "properties": {
        "project": {
          "type": "string",
          "description": "project name; omitted resumes the last project"
        },
        "session_id": {
          "type": "string",
          "description": "specific Claude session id; omitted resumes the newest"
        },
        "cwd": {
          "type": "string",
          "description": "project directory for a name that is not discovered"
        },
        "harness": {
          "type": "string",
          "description": "claude, codex or freebuff; defaults to the project's remembered harness"
        },
        "prompt": {
          "type": "string",
          "description": "optional prompt sent after resume"
        },
        "timeout_s": {
          "type": "number"
        },
        "idle_exit_s": {
          "type": "number"
        },
        "tools": {
          "type": "boolean"
        },
        "wait": {
          "type": "boolean",
          "description": "true blocks until the prompted turn ends; false (default) returns a turn_id at once"
        },
        "fork": {
          "type": "boolean",
          "description": "fork the resumed session instead of reusing its id"
        },
        "trust": {
          "type": "boolean"
        }
      },
      "additionalProperties": false
    }
  },
  {
    "name": "list_project_sessions",
    "description": "List resumable harness sessions for one project, or for every discovered project and harness with all=true. Shows session ids with timestamps and paths.",
    "inputSchema": {
      "type": "object",
      "properties": {
        "project": {
          "type": "string"
        },
        "cwd": {
          "type": "string"
        },
        "harness": {
          "type": "string"
        },
        "all": {
          "type": "boolean"
        }
      },
      "additionalProperties": false
    }
  },
  {
    "name": "peer_status",
    "description": "List live peers with alive and busy state, cwd, harness, launch args and pane tail.",
    "inputSchema": {
      "type": "object",
      "properties": {},
      "additionalProperties": false
    }
  },
  {
    "name": "read_pane",
    "description": "Print the tail of a peer's terminal pane.",
    "inputSchema": {
      "type": "object",
      "properties": {
        "name": {
          "type": "string"
        },
        "lines": {
          "type": "number"
        }
      },
      "required": [
        "name"
      ],
      "additionalProperties": false
    }
  },
  {
    "name": "stop_peer",
    "description": "Retire one peer session.",
    "inputSchema": {
      "type": "object",
      "properties": {
        "name": {
          "type": "string"
        }
      },
      "required": [
        "name"
      ],
      "additionalProperties": false
    }
  },
  {
    "name": "server_down",
    "description": "Stop the airc server and every peer it owns.",
    "inputSchema": {
      "type": "object",
      "properties": {},
      "additionalProperties": false
    }
  },
  {
    "name": "list_outputs",
    "description": "List journaled session output files with agent, session, harness, cwd, size, last record time and a short last-output preview.",
    "inputSchema": {
      "type": "object",
      "properties": {
        "project": {
          "type": "string"
        },
        "cwd": {
          "type": "string"
        },
        "agent": {
          "type": "string"
        },
        "session_id": {
          "type": "string"
        },
        "limit": {
          "type": "number"
        }
      },
      "additionalProperties": false
    }
  },
  {
    "name": "read_output",
    "description": "Read one journaled session output as a compact transcript, newest records last. Filter by kinds, tail count, since epoch, or format=json.",
    "inputSchema": {
      "type": "object",
      "properties": {
        "project": {
          "type": "string"
        },
        "cwd": {
          "type": "string"
        },
        "agent": {
          "type": "string"
        },
        "session_id": {
          "type": "string"
        },
        "kinds": {
          "type": "array",
          "items": {
            "type": "string"
          },
          "description": "text, tool, you, instruct, result, raw"
        },
        "tail": {
          "type": "number"
        },
        "since": {
          "type": "number",
          "description": "epoch seconds"
        },
        "max_chars": {
          "type": "number"
        },
        "per_record_chars": {
          "type": "number"
        },
        "format": {
          "type": "string",
          "enum": [
            "text",
            "json"
          ]
        }
      },
      "additionalProperties": false
    }
  },
  {
    "name": "search_output",
    "description": "Search every journaled session output for a substring and return matching records with agent, session, kind, time and path.",
    "inputSchema": {
      "type": "object",
      "properties": {
        "query": {
          "type": "string"
        },
        "project": {
          "type": "string"
        },
        "cwd": {
          "type": "string"
        },
        "agent": {
          "type": "string"
        },
        "session_id": {
          "type": "string"
        },
        "limit": {
          "type": "number"
        },
        "max_chars": {
          "type": "number"
        },
        "ignore_case": {
          "type": "boolean"
        }
      },
      "required": [
        "query"
      ],
      "additionalProperties": false
    }
  }
])schema";

inline constexpr const char* instructions = R"schema(Peer agents are project directories beside this one. list_projects launches nothing. start_projects starts only the names given and runs Claude Code in each project through airc. instruct and instruct_many prompt peers and return their turns; wait=false returns a turn_id for turn_result. resume_project continues a project's last conversation in claude, codex or freebuff (or a named one) after its peer stopped; list_project_sessions shows the candidates. Peers keep their conversation across prompts and stay alive until stop_peer or server_down. Every turn is journaled per session under .orch/projects; list_outputs, read_output and search_output are the MCP views, and resources under hyperagent://outputs read the same files. The peer monitor emits an idle event at turn end (trigger result or pane) with the whole last response; subscribe with resources/subscribe or subscriptions/listen (resourceSubscriptions, resourcesListChanged), follow logging/setLevel for notifications/message, and read hyperagent://events for the recent event stream. Run bin/watch locally for the same events as lines.)schema";

}

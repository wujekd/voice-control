#!/usr/bin/env bash
set -eo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: bundle_ffmpeg_macos.sh /path/to/ffmpeg /path/to/App.app/Contents/Resources" >&2
    exit 2
fi

ffmpeg_source="$1"
resources_dir="$2"
ffmpeg_dest="${resources_dir}/ffmpeg"
lib_dir="${resources_dir}/lib"

mkdir -p "${lib_dir}"
cp -f "${ffmpeg_source}" "${ffmpeg_dest}"
chmod u+w,go+rx "${ffmpeg_dest}"

is_system_library() {
    case "$1" in
        /System/Library/*|/usr/lib/*)
            return 0
            ;;
        @executable_path/*|@loader_path/*)
            return 0
            ;;
    esac

    return 1
}

resolve_dependency() {
    local dependency="$1"
    local dependency_name

    case "${dependency}" in
        @rpath/*)
            dependency_name="$(basename "${dependency}")"
            find /opt/homebrew /usr/local -name "${dependency_name}" -print -quit 2>/dev/null || true
            ;;
        *)
            printf '%s\n' "${dependency}"
            ;;
    esac
}

declare -a queue=("${ffmpeg_dest}")
declare -a seen=()

has_seen() {
    local needle="$1"
    local item
    for item in "${seen[@]}"; do
        [[ "${item}" == "${needle}" ]] && return 0
    done

    return 1
}

while ((${#queue[@]} > 0)); do
    current="${queue[0]}"
    queue=("${queue[@]:1}")

    has_seen "${current}" && continue
    seen+=("${current}")

    chmod u+w "${current}"

    if [[ "${current}" == *.dylib ]]; then
        install_name_tool -id "@loader_path/$(basename "${current}")" "${current}" 2>/dev/null || true
    fi

    while IFS= read -r dependency; do
        [[ -z "${dependency}" ]] && continue
        dependency_reference="${dependency}"
        is_system_library "${dependency}" && continue

        dependency="$(resolve_dependency "${dependency}")"
        [[ -n "${dependency}" ]] || continue
        [[ -f "${dependency}" ]] || continue

        dependency_name="$(basename "${dependency}")"
        dependency_dest="${lib_dir}/${dependency_name}"

        if [[ ! -f "${dependency_dest}" ]]; then
            cp -f "${dependency}" "${dependency_dest}"
            chmod u+w,go+r "${dependency_dest}"
        fi

        if ! has_seen "${dependency_dest}"; then
            queue+=("${dependency_dest}")
        fi

        if [[ "${current}" == "${ffmpeg_dest}" ]]; then
            replacement="@executable_path/lib/${dependency_name}"
        else
            replacement="@loader_path/${dependency_name}"
        fi

        install_name_tool -change "${dependency_reference}" "${replacement}" "${current}" 2>/dev/null || true
    done < <(otool -L "${current}" | awk 'NR > 1 { print $1 }')
done

while IFS= read -r dylib; do
    codesign --force --sign - "${dylib}" >/dev/null
done < <(find "${lib_dir}" -type f -name '*.dylib')

codesign --force --sign - "${ffmpeg_dest}" >/dev/null

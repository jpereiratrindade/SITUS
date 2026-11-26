#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
cd "$ROOT_DIR"

if [ -f .env ]; then
    set -a
    # shellcheck disable=SC1091
    source .env
    set +a
fi

DB_CONTAINER="${SITUS_DB_CONTAINER:-situs-postgis-db}"
DB_VOLUME="${SITUS_DB_VOLUME:-situs_pgdata}"
DB_IMAGE="${SITUS_DB_IMAGE:-docker.io/postgis/postgis:17-3.5}"
DB_HOST="${SITUS_DB_HOST:-127.0.0.1}"
DB_PORT="${SITUS_DB_PORT:-55436}"
DB_NAME="${SITUS_DB_NAME:-situs}"
DB_ADMIN_USER="${SITUS_DB_ADMIN_USER:-situs_migrator}"
DB_ADMIN_PASSWORD="${SITUS_DB_ADMIN_PASSWORD:-change_me_migrator_local}"
DB_QUERY_USER="${SITUS_DB_QUERY_USER:-situs_reader}"
DB_QUERY_PASSWORD="${SITUS_DB_QUERY_PASSWORD:-change_me_reader_local}"

cmd="${1:-status}"

case "$cmd" in
    start)
        if podman container exists "$DB_CONTAINER"; then
            podman start "$DB_CONTAINER" >/dev/null
        else
            podman volume create "$DB_VOLUME" >/dev/null
            podman run -d \
                --name "$DB_CONTAINER" \
                -e POSTGRES_DB="$DB_NAME" \
                -e POSTGRES_USER="$DB_ADMIN_USER" \
                -e POSTGRES_PASSWORD="$DB_ADMIN_PASSWORD" \
                -p "${DB_HOST}:${DB_PORT}:5432" \
                -v "${DB_VOLUME}:/var/lib/postgresql/data:Z" \
                "$DB_IMAGE" >/dev/null
        fi
        echo "Aguardando PostGIS em ${DB_HOST}:${DB_PORT}..."
        for _ in $(seq 1 30); do
            if podman exec "$DB_CONTAINER" pg_isready -U "$DB_ADMIN_USER" -d "$DB_NAME" >/dev/null 2>&1; then
                echo "[OK] PostGIS pronto em ${DB_HOST}:${DB_PORT}/${DB_NAME}"
                exit 0
            fi
            sleep 1
        done
        echo "[ERRO] Timeout aguardando PostGIS" >&2
        exit 1
        ;;
    stop)
        podman stop "$DB_CONTAINER" >/dev/null || true
        echo "[OK] Container $DB_CONTAINER parado"
        ;;
    status)
        podman ps -a --filter "name=${DB_CONTAINER}" --format "table {{.Names}}\t{{.Image}}\t{{.Status}}\t{{.Ports}}"
        ;;
    migrate)
        for f in db/migrations/*.sql; do
            ver="$(basename "$f" | cut -d_ -f1)"
            applied="$(podman exec "$DB_CONTAINER" psql -At -U "$DB_ADMIN_USER" -d "$DB_NAME" -c "SELECT 1 FROM schema_migration WHERE version = '${ver}'" 2>/dev/null || true)"
            if [ "$applied" = "1" ]; then
                echo "Pulando $f (versao ${ver} ja aplicada)"
                continue
            fi
            echo "Aplicando $f..."
            podman exec -i "$DB_CONTAINER" psql -v ON_ERROR_STOP=1 -U "$DB_ADMIN_USER" -d "$DB_NAME" < "$f"
        done
        podman exec -i "$DB_CONTAINER" psql -v ON_ERROR_STOP=1 -U "$DB_ADMIN_USER" -d "$DB_NAME" <<SQL
DO \$\$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = '${DB_QUERY_USER}') THEN
        EXECUTE format('CREATE ROLE %I LOGIN PASSWORD %L', '${DB_QUERY_USER}', '${DB_QUERY_PASSWORD}');
    END IF;
END
\$\$;
GRANT CONNECT ON DATABASE ${DB_NAME} TO ${DB_QUERY_USER};
GRANT USAGE ON SCHEMA public TO ${DB_QUERY_USER};
GRANT SELECT ON ALL TABLES IN SCHEMA public TO ${DB_QUERY_USER};
ALTER DEFAULT PRIVILEGES IN SCHEMA public GRANT SELECT ON TABLES TO ${DB_QUERY_USER};
SQL
        echo "[OK] Migracoes aplicadas e usuario de leitura '${DB_QUERY_USER}' configurado"
        ;;
    *)
        echo "Uso: $0 {start|stop|status|migrate}" >&2
        exit 1
        ;;
esac

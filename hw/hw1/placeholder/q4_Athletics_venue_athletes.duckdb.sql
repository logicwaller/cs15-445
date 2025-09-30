with athletics_venue_participations as(
    select participant_code 
    from venues, results
    where venues.disciplines like '%Athletics%'
        and venues.venue = results.venue
),
team_member_athletics_venue as(
    select code, name, country_code, nationality_code
    from 
        (select athletes_code
        from athletics_venue_participations as av 
            left join teams on av.participant_code = teams.code) as t
        join athletes on t.athletes_code = athletes.code
),
aths_athletics_venue as(
    select * from team_member_athletics_venue
    union
    (select code, name, country_code, nationality_code
    from athletics_venue_participations as av
        left join athletes on av.participant_code = athletes.code)
)

select name as athlete_name, 
    country_code as represented_country_code, 
    nationality_code as nationality_country_code, 
from (
    select *, countries.Latitude as cou_x, countries.Longitude as cou_y
    from (select *, countries.Latitude as nat_x, countries.Longitude as nat_y
            from aths_athletics_venue as aths 
            left join countries
            on aths.nationality_code = countries.code) as a
        left join countries
        on a.country_code = countries.code
    where cou_x is not null
        and cou_y is not null
        and nat_x is not null
        and nat_y is not null
)
order by (select (cou_x - nat_x)^2 + (cou_y - nat_y) ^ 2 ) desc, athlete_name;